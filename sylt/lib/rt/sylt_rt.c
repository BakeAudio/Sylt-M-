#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    long long len;
    char* data;
} SyStr;

typedef struct {
    long long len;
    long long cap;
    char* data;
} SyArr;

typedef struct {
    long long count;
    long long cap;
    long long kkind;
    long long vsize;
    char* ent;
} SyMap;

static void sy_fatal(const char* msg) {
    fprintf(stderr, "error en ejecución: %s\n", msg);
    exit(1);
}

static void* sy_xmalloc(long long n) {
    void* p = malloc((size_t)(n > 0 ? n : 1));
    if (!p) sy_fatal("me quedé sin memoria");
    return p;
}

static char* sy_strdup_n(const char* s, long long n) {
    char* r = (char*)sy_xmalloc(n + 1);
    memcpy(r, s, (size_t)n);
    r[n] = 0;
    return r;
}

static SyStr* sy_str_new_n(const char* s, long long n) {
    SyStr* r = (SyStr*)sy_xmalloc((long long)sizeof(SyStr));
    r->len = n;
    r->data = sy_strdup_n(s, n);
    return r;
}

void* sy_str_from_c(char* s) {
    return sy_str_new_n(s, (long long)strlen(s));
}

void* sy_str_clona(void* s) {
    SyStr* a = (SyStr*)s;
    return sy_str_new_n(a->data, a->len);
}

char* sy_str_bytes(void* s) {
    return ((SyStr*)s)->data;
}

long long sy_str_bytes_len(void* s) {
    return ((SyStr*)s)->len;
}

void SyltAbort(char* msg) {
    sy_fatal(msg);
}

void* sy_arr_nuevo(long long elemsize) {
    SyArr* a = (SyArr*)sy_xmalloc((long long)sizeof(SyArr));
    a->len = 0;
    a->cap = 4;
    a->data = (char*)sy_xmalloc(elemsize * a->cap);
    return a;
}

long long sy_arr_len(void* arr) {
    return ((SyArr*)arr)->len;
}

void sy_arr_push(void* arr, void* elem, long long elemsize) {
    SyArr* a = (SyArr*)arr;
    if (a->len == a->cap) {
        a->cap *= 2;
        char* nd = (char*)sy_xmalloc(elemsize * a->cap);
        memcpy(nd, a->data, (size_t)(a->len * elemsize));
        a->data = nd;
    }
    memcpy(a->data + a->len * elemsize, elem, (size_t)elemsize);
    a->len += 1;
}

static void sy_arr_rango(long long i, long long n, const char* nombre) {
    char buf[512];
    snprintf(buf, sizeof(buf), "índice %lld fuera de rango en `%s` (Len=%lld)", i, nombre, n);
    sy_fatal(buf);
}

void* sy_arr_slot(void* arr, long long i, long long elemsize, char* nombre) {
    SyArr* a = (SyArr*)arr;
    if (i < 0 || i >= a->len) sy_arr_rango(i, a->len, nombre ? nombre : "?");
    return a->data + i * elemsize;
}

void* sy_arr_pop(void* arr, long long elemsize) {
    SyArr* a = (SyArr*)arr;
    if (a->len == 0) sy_fatal("Pop() sobre un array vacío");
    a->len -= 1;
    void* r = sy_xmalloc(elemsize);
    memcpy(r, a->data + a->len * elemsize, (size_t)elemsize);
    return r;
}

int sy_arr_tiene(void* arr, void* elem, long long elemsize) {
    SyArr* a = (SyArr*)arr;
    for (long long i = 0; i < a->len; i++)
        if (memcmp(a->data + i * elemsize, elem, (size_t)elemsize) == 0) return 1;
    return 0;
}

int sy_arr_tiene_str(void* arr, void* s) {
    SyArr* a = (SyArr*)arr;
    SyStr* b = (SyStr*)s;
    for (long long i = 0; i < a->len; i++) {
        SyStr* e = *(SyStr**)(a->data + i * 8);
        if (e->len == b->len && memcmp(e->data, b->data, (size_t)b->len) == 0) return 1;
    }
    return 0;
}

void* sy_arr_copia(void* arr, long long elemsize, int elem_str) {
    SyArr* a = (SyArr*)arr;
    SyArr* r = (SyArr*)sy_arr_nuevo(elemsize);
    for (long long i = 0; i < a->len; i++) {
        char* slot = a->data + i * elemsize;
        if (elem_str) {
            SyStr* c = (SyStr*)sy_str_clona(*(SyStr**)slot);
            sy_arr_push(r, &c, elemsize);
        } else {
            sy_arr_push(r, slot, elemsize);
        }
    }
    return r;
}

static int sy_key_cmp(SyMap* m, void* ka, void* kb) {
    if (m->kkind == 0) {
        SyStr* a = (SyStr*)ka;
        SyStr* b = (SyStr*)kb;
        long long n = a->len < b->len ? a->len : b->len;
        int c = n > 0 ? memcmp(a->data, b->data, (size_t)n) : 0;
        if (c != 0) return c < 0 ? -1 : 1;
        if (a->len == b->len) return 0;
        return a->len < b->len ? -1 : 1;
    }
    long long a = *(long long*)ka, b = *(long long*)kb;
    if (a == b) return 0;
    return a < b ? -1 : 1;
}

static long long sy_key_size(SyMap* m) {
    return m->kkind == 0 ? (long long)sizeof(SyStr) : 8;
}

void* sy_map_nuevo(long long kkind, long long valsize) {
    SyMap* m = (SyMap*)sy_xmalloc((long long)sizeof(SyMap));
    m->count = 0;
    m->cap = 8;
    m->kkind = kkind;
    m->vsize = valsize;
    m->ent = (char*)sy_xmalloc((sy_key_size(m) + valsize) * m->cap);
    return m;
}

long long sy_map_len(void* map) {
    return ((SyMap*)map)->count;
}

static void* sy_map_busca(SyMap* m, void* key, long long valsize) {
    (void)valsize;
    long long ks = sy_key_size(m);
    long long es = ks + m->vsize;
    long long lo = 0, hi = m->count - 1;
    while (lo <= hi) {
        long long mid = (lo + hi) / 2;
        char* ek = m->ent + mid * es;
        int c = sy_key_cmp(m, ek, key);
        if (c == 0) return m->ent + mid * es + ks;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

int sy_map_tiene_str(void* map, void* key) {
    return sy_map_busca((SyMap*)map, key, 0) != NULL;
}

int sy_map_tiene_int(void* map, long long key) {
    return sy_map_busca((SyMap*)map, &key, 0) != NULL;
}

static void sy_map_falta(SyMap* m, void* key, const char* nombre) {
    char buf[640];
    if (m->kkind == 0) {
        SyStr* k = (SyStr*)key;
        snprintf(buf, sizeof(buf), "la clave %.*s no existe en `%s`", (int)k->len, k->data,
                 nombre ? nombre : "?");
    } else {
        snprintf(buf, sizeof(buf), "la clave %lld no existe en `%s`", *(long long*)key,
                 nombre ? nombre : "?");
    }
    sy_fatal(buf);
}

static void* sy_map_slot_ins(SyMap* m, void* key, long long valsize, int insertar,
                             const char* nombre) {
    long long ks = sy_key_size(m);
    if (valsize == 0) valsize = m->vsize;
    long long lo = 0, hi = m->count - 1, pos = 0;
    while (lo <= hi) {
        long long mid = (lo + hi) / 2;
        char* ek = m->ent + mid * (ks + valsize);
        int c = sy_key_cmp(m, ek, key);
        if (c == 0) return ek + ks;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    if (!insertar) sy_map_falta(m, key, nombre);
    pos = lo;
    if (m->count == m->cap) {
        m->cap *= 2;
        char* ne = (char*)sy_xmalloc((ks + valsize) * m->cap);
        memcpy(ne, m->ent, (size_t)(m->count * (ks + valsize)));
        m->ent = ne;
    }
    long long es = ks + valsize;
    memmove(m->ent + (pos + 1) * es, m->ent + pos * es, (size_t)((m->count - pos) * es));
    char* slot = m->ent + pos * es;
    if (m->kkind == 0) {
        SyStr* k = (SyStr*)key;
        SyStr nk;
        nk.len = k->len;
        nk.data = sy_strdup_n(k->data, k->len);
        memcpy(slot, &nk, sizeof(SyStr));
    } else {
        memcpy(slot, key, 8);
    }
    memset(slot + ks, 0, (size_t)valsize);
    m->count += 1;
    return slot + ks;
}

void* sy_map_pon_str(void* map, void* key, void* val, long long valsize) {
    void* s = sy_map_slot_ins((SyMap*)map, key, valsize, 1, NULL);
    memcpy(s, val, (size_t)valsize);
    return map;
}

void* sy_map_pon_int(void* map, long long key, void* val, long long valsize) {
    void* s = sy_map_slot_ins((SyMap*)map, &key, valsize, 1, NULL);
    memcpy(s, val, (size_t)valsize);
    return map;
}

void* sy_map_slot_str(void* map, void* key, long long valsize, char* nombre) {
    return sy_map_slot_ins((SyMap*)map, key, valsize, 0, nombre);
}

void* sy_map_slot_int(void* map, long long key, long long valsize, char* nombre) {
    return sy_map_slot_ins((SyMap*)map, &key, valsize, 0, nombre);
}

void* sy_map_slot_ins_str(void* map, void* key, long long valsize) {
    return sy_map_slot_ins((SyMap*)map, key, valsize, 1, NULL);
}

void* sy_map_slot_ins_int(void* map, long long key, long long valsize) {
    return sy_map_slot_ins((SyMap*)map, &key, valsize, 1, NULL);
}

void* sy_map_clave_str(void* map, long long i) {
    SyMap* m = (SyMap*)map;
    return m->ent + i * (sy_key_size(m) + m->vsize);
}

long long sy_map_clave_int(void* map, long long i) {
    SyMap* m = (SyMap*)map;
    return *(long long*)(m->ent + i * (sy_key_size(m) + m->vsize));
}

void* sy_map_valor(void* map, long long i, long long valsize) {
    SyMap* m = (SyMap*)map;
    return m->ent + i * (sy_key_size(m) + valsize) + sy_key_size(m);
}

void* sy_map_copia(void* map, long long kkind, long long valsize, int val_str) {
    SyMap* m = (SyMap*)map;
    SyMap* r = (SyMap*)sy_map_nuevo(kkind, valsize);
    for (long long i = 0; i < m->count; i++) {
        void* k = kkind == 0 ? (void*)sy_map_clave_str(m, i) : NULL;
        long long ki = kkind == 0 ? 0 : sy_map_clave_int(m, i);
        void* v = sy_map_valor(m, i, valsize);
        if (val_str) {
            SyStr* c = (SyStr*)sy_str_clona(*(SyStr**)v);
            if (kkind == 0) sy_map_pon_str(r, k, &c, valsize);
            else sy_map_pon_int(r, ki, &c, valsize);
        } else {
            if (kkind == 0) sy_map_pon_str(r, k, v, valsize);
            else sy_map_pon_int(r, ki, v, valsize);
        }
    }
    return r;
}

void* sy_map_claves(void* map, long long kkind) {
    SyMap* m = (SyMap*)map;
    if (kkind == 0) {
        SyArr* r = (SyArr*)sy_arr_nuevo(8);
        for (long long i = 0; i < m->count; i++) {
            SyStr* k = (SyStr*)sy_map_clave_str(m, i);
            SyStr* c = (SyStr*)sy_str_clona(k);
            sy_arr_push(r, &c, 8);
        }
        return r;
    }
    SyArr* r = (SyArr*)sy_arr_nuevo(8);
    for (long long i = 0; i < m->count; i++) {
        long long k = sy_map_clave_int(m, i);
        sy_arr_push(r, &k, 8);
    }
    return r;
}

char* sy_str_dividir(char* s, char* sep) {
    long long sl = (long long)strlen(s);
    long long pl = (long long)strlen(sep);
    SyArr* r = (SyArr*)sy_arr_nuevo(8);
    if (pl == 0) {
        SyStr* c = sy_str_new_n(s, sl);
        sy_arr_push(r, &c, 8);
        return (char*)r;
    }
    long long ini = 0;
    while (1) {
        char* p = NULL;
        long long rest = sl - ini;
        if (rest >= pl) p = (char*)memmem(s + ini, (size_t)rest, sep, (size_t)pl);
        if (!p) {
            SyStr* c = sy_str_new_n(s + ini, sl - ini);
            sy_arr_push(r, &c, 8);
            break;
        }
        long long at = (long long)(p - s);
        SyStr* c = sy_str_new_n(s + ini, at - ini);
        sy_arr_push(r, &c, 8);
        ini = at + pl;
    }
    return (char*)r;
}

char* sy_str_unir(void* arr, char* sep) {
    SyArr* a = (SyArr*)arr;
    long long pl = (long long)strlen(sep);
    long long tot = 0;
    for (long long i = 0; i < a->len; i++) {
        if (i) tot += pl;
        tot += (*(SyStr**)(a->data + i * 8))->len;
    }
    char* r = (char*)sy_xmalloc(tot + 1);
    long long w = 0;
    for (long long i = 0; i < a->len; i++) {
        if (i) {
            memcpy(r + w, sep, (size_t)pl);
            w += pl;
        }
        SyStr* e = *(SyStr**)(a->data + i * 8);
        memcpy(r + w, e->data, (size_t)e->len);
        w += e->len;
    }
    r[w] = 0;
    return r;
}

char* sy_str_caracteres(char* s) {
    long long n = (long long)strlen(s);
    SyArr* r = (SyArr*)sy_arr_nuevo(8);
    long long i = 0;
    while (i < n) {
        long long l = 1;
        unsigned char c = (unsigned char)s[i];
        if (c >= 0xF0) l = 4;
        else if (c >= 0xE0) l = 3;
        else if (c >= 0xC0) l = 2;
        if (i + l > n) l = n - i;
        SyStr* c2 = sy_str_new_n(s + i, l);
        sy_arr_push(r, &c2, 8);
        i += l;
    }
    return (char*)r;
}

static void sy_trim2(char** a, long long* n) {
    while (*n > 0 && (unsigned char)(*a)[0] <= ' ') {
        (*a)++;
        (*n)--;
    }
    while (*n > 0 && (unsigned char)(*a)[*n - 1] <= ' ') (*n)--;
}

long long sy_str_a_entero(char* s) {
    char* a = s;
    long long n = (long long)strlen(s);
    sy_trim2(&a, &n);
    char* copia = sy_strdup_n(a, n);
    long long r = (long long)strtoll(copia, NULL, 10);
    free(copia);
    return r;
}

double sy_str_a_float(char* s) {
    char* a = s;
    long long n = (long long)strlen(s);
    sy_trim2(&a, &n);
    char* copia = sy_strdup_n(a, n);
    double r = strtod(copia, NULL);
    free(copia);
    return r;
}

char* sy_str_recortar(char* s) {
    char* a = s;
    long long n = (long long)strlen(s);
    sy_trim2(&a, &n);
    return sy_strdup_n(a, n);
}

char* sy_str_mayusculas(char* s) {
    long long n = (long long)strlen(s);
    char* r = sy_strdup_n(s, n);
    for (long long i = 0; i < n; i++) r[i] = (char)toupper((unsigned char)r[i]);
    return r;
}

char* sy_str_minusculas(char* s) {
    long long n = (long long)strlen(s);
    char* r = sy_strdup_n(s, n);
    for (long long i = 0; i < n; i++) r[i] = (char)tolower((unsigned char)r[i]);
    return r;
}

char* sy_str_reemplazar(char* s, char* de, char* a) {
    long long dl = (long long)strlen(de);
    if (dl == 0) return sy_strdup_n(s, (long long)strlen(s));
    long long sl = (long long)strlen(s);
    long long al = (long long)strlen(a);
    char* tmp = (char*)sy_xmalloc(sl * 4 + al * 4 + 16);
    long long w = 0;
    long long i = 0;
    while (i < sl) {
        long long rest = sl - i;
        char* p = rest >= dl ? (char*)memmem(s + i, (size_t)rest, de, (size_t)dl) : NULL;
        if (!p) {
            memcpy(tmp + w, s + i, (size_t)(sl - i));
            w += sl - i;
            break;
        }
        long long at = (long long)(p - s);
        memcpy(tmp + w, s + i, (size_t)(at - i));
        w += at - i;
        memcpy(tmp + w, a, (size_t)al);
        w += al;
        i = at + dl;
    }
    char* r = sy_strdup_n(tmp, w);
    free(tmp);
    return r;
}

int sy_str_empieza_con(char* s, char* p) {
    size_t ls = strlen(s), lp = strlen(p);
    return ls >= lp && memcmp(s, p, lp) == 0;
}

int sy_str_termina_con(char* s, char* p) {
    size_t ls = strlen(s), lp = strlen(p);
    return ls >= lp && memcmp(s + ls - lp, p, lp) == 0;
}

long long sy_str_buscar(char* s, char* sub) {
    char* p = strstr(s, sub);
    if (!p) return -1;
    return (long long)(p - s);
}

char* sy_str_repetir(char* s, long long n) {
    if (n < 0) n = 0;
    if (n > 1000000) n = 1000000;
    long long l = (long long)strlen(s);
    char* r = (char*)sy_xmalloc(l * n + 1);
    for (long long i = 0; i < n; i++) memcpy(r + i * l, s, (size_t)l);
    r[l * n] = 0;
    return r;
}

char* sy_str_subcadena(char* s, long long ini, long long n) {
    long long l = (long long)strlen(s);
    if (ini < 0) {
        ini = l + ini;
        if (ini < 0) ini = 0;
    }
    if (ini >= l) return sy_strdup_n("", 0);
    if (n < 0) n = 0;
    if (ini + n > l) n = l - ini;
    return sy_strdup_n(s + ini, n);
}

long long sy_str_codigo(char* s) {
    long long n = (long long)strlen(s);
    if (n == 0) return 0;
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return c;
    long long l = c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : (c >= 0xC0 ? 2 : 1));
    if (l > n) return c;
    long long r = c & (0x7F >> (int)l);
    for (long long i = 1; i < l; i++) r = (r << 6) | ((unsigned char)s[i] & 0x3F);
    return r;
}

char* sy_str_desde_codigo(long long n) {
    if (n < 0 || n > 0x10FFFF) return sy_strdup_n("", 0);
    if (n < 0x80) {
        char c = (char)n;
        return sy_strdup_n(&c, 1);
    }
    char b[5];
    long long l = 0;
    if (n < 0x800) {
        b[l++] = (char)(0xC0 | (n >> 6));
        b[l++] = (char)(0x80 | (n & 0x3F));
    } else if (n < 0x10000) {
        b[l++] = (char)(0xE0 | (n >> 12));
        b[l++] = (char)(0x80 | ((n >> 6) & 0x3F));
        b[l++] = (char)(0x80 | (n & 0x3F));
    } else {
        b[l++] = (char)(0xF0 | (n >> 18));
        b[l++] = (char)(0x80 | ((n >> 12) & 0x3F));
        b[l++] = (char)(0x80 | ((n >> 6) & 0x3F));
        b[l++] = (char)(0x80 | (n & 0x3F));
    }
    return sy_strdup_n(b, l);
}

char* sy_f64_prec(double v, long long d) {
    if (d < 0) d = 0;
    if (d > 17) d = 17;
    char buf[128];
    snprintf(buf, sizeof(buf), "%.*f", (int)d, v);
    return sy_strdup_n(buf, (long long)strlen(buf));
}

char* sy_io_leer_linea(void) {
    char* r = (char*)sy_xmalloc(65536);
    if (!fgets(r, 65536, stdin)) {
        free(r);
        return sy_strdup_n("", 0);
    }
    long long n = (long long)strlen(r);
    while (n > 0 && (r[n - 1] == '\n' || r[n - 1] == '\r')) n--;
    char* out = sy_strdup_n(r, n);
    free(r);
    return out;
}

char* sy_io_leer_archivo(char* ruta) {
    FILE* f = fopen(ruta, "rb");
    if (!f) {
        char buf[512];
        snprintf(buf, sizeof(buf), "no puedo leer el archivo `%s`", ruta);
        sy_fatal(buf);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* r = (char*)sy_xmalloc((long long)n + 1);
    if (n > 0) {
        size_t got = fread(r, 1, (size_t)n, f);
        r[got] = 0;
    } else {
        r[0] = 0;
    }
    fclose(f);
    return r;
}

void sy_io_escribir_archivo(char* ruta, char* contenido) {
    FILE* f = fopen(ruta, "wb");
    if (!f) {
        char buf[512];
        snprintf(buf, sizeof(buf), "no puedo escribir el archivo `%s`", ruta);
        sy_fatal(buf);
    }
    fputs(contenido, f);
    fclose(f);
}

void sy_io_anexar_archivo(char* ruta, char* linea) {
    FILE* f = fopen(ruta, "a");
    if (!f) {
        char buf[512];
        snprintf(buf, sizeof(buf), "no puedo anexar al archivo `%s`", ruta);
        sy_fatal(buf);
    }
    fputs(linea, f);
    fclose(f);
}

int sy_io_existe(char* ruta) {
    FILE* f = fopen(ruta, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

long long sy_time_epoch_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

double sy_mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

long long sy_net_conectar(char* host, long long puerto) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char ps[16];
    snprintf(ps, sizeof(ps), "%lld", puerto);
    if (getaddrinfo(host, ps, &hints, &res) != 0 || !res) return -1;
    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

void sy_net_enviar(long long fd, char* datos) {
    size_t total = strlen(datos), enviado = 0;
    while (enviado < total) {
        ssize_t n = send((int)fd, datos + enviado, total - enviado, 0);
        if (n <= 0) sy_fatal("fallo al enviar por el socket");
        enviado += (size_t)n;
    }
}

char* sy_net_recibir(long long fd, long long max) {
    if (max < 1) max = 4096;
    if (max > (1 << 20)) max = (1 << 20);
    char* buf = (char*)sy_xmalloc(max + 1);
    ssize_t n = recv((int)fd, buf, (size_t)max, 0);
    if (n < 0) n = 0;
    buf[n] = 0;
    return buf;
}

void sy_net_cerrar(long long fd) {
    close((int)fd);
}

long long sy_net_escuchar(long long puerto) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) sy_fatal("no puedo crear el socket del servidor");
    int uno = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof(uno));
    struct sockaddr_in dir;
    memset(&dir, 0, sizeof(dir));
    dir.sin_family = AF_INET;
    dir.sin_addr.s_addr = htonl(INADDR_ANY);
    dir.sin_port = htons((uint16_t)puerto);
    if (bind(fd, (struct sockaddr*)&dir, sizeof(dir)) != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "no puedo enlazar el puerto %lld (¿está ocupado?)", puerto);
        sy_fatal(buf);
    }
    if (listen(fd, 16) != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "no puedo escuchar en el puerto %lld", puerto);
        sy_fatal(buf);
    }
    return fd;
}

long long sy_net_aceptar(long long fd) {
    int c = accept((int)fd, NULL, NULL);
    if (c < 0) sy_fatal("fallo al aceptar una conexión");
    return c;
}

char* sy_net_direccion(long long fd) {
    struct sockaddr_in dir;
    socklen_t l = sizeof(dir);
    if (getpeername((int)fd, (struct sockaddr*)&dir, &l) != 0) return sy_strdup_n("?", 1);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s:%u", inet_ntoa(dir.sin_addr), (unsigned)ntohs(dir.sin_port));
    return sy_strdup_n(buf, (long long)strlen(buf));
}

long long sy_vigia_crear(void) {
    return epoll_create1(0);
}

int sy_vigia_vigilar(long long v, long long fd) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = (int)fd;
    return epoll_ctl((int)v, EPOLL_CTL_ADD, (int)fd, &ev) == 0;
}

long long sy_vigia_esperar(long long v, long long timeout_ms) {
    struct epoll_event ev;
    int n = epoll_wait((int)v, &ev, 1, (int)timeout_ms);
    if (n <= 0) return -1;
    return ev.data.fd;
}

int sy_net_listo(long long fd, long long ms) {
    struct pollfd p;
    p.fd = (int)fd;
    p.events = POLLIN;
    p.revents = 0;
    int r = poll(&p, 1, (int)ms);
    return r > 0 && (p.revents & POLLIN);
}

typedef struct {
    long long tag;
    union {
        long long i;
        double d;
        struct {
            long long len;
            char* data;
        } s;
    } v;
} SyItem;

typedef struct {
    long long n;
    long long cap;
    SyItem* items;
} SyPack;

void* sy_paquete_nuevo(void) {
    SyPack* p = (SyPack*)sy_xmalloc((long long)sizeof(SyPack));
    p->n = 0;
    p->cap = 8;
    p->items = (SyItem*)sy_xmalloc((long long)sizeof(SyItem) * p->cap);
    return p;
}

static void sy_pack_pon(SyPack* p, long long tag) {
    if (p->n == p->cap) {
        p->cap *= 2;
        SyItem* ni = (SyItem*)sy_xmalloc((long long)sizeof(SyItem) * p->cap);
        memcpy(ni, p->items, (size_t)((long long)sizeof(SyItem) * p->n));
        p->items = ni;
    }
    p->items[p->n].tag = tag;
    p->n += 1;
}

void* sy_paquete_int(void* pack, long long v) {
    SyPack* p = (SyPack*)pack;
    sy_pack_pon(p, 0);
    p->items[p->n - 1].v.i = v;
    return p;
}

void* sy_paquete_float(void* pack, double v) {
    SyPack* p = (SyPack*)pack;
    sy_pack_pon(p, 1);
    p->items[p->n - 1].v.d = v;
    return p;
}

void* sy_paquete_bool(void* pack, int v) {
    SyPack* p = (SyPack*)pack;
    sy_pack_pon(p, 2);
    p->items[p->n - 1].v.i = v;
    return p;
}

void* sy_paquete_str(void* pack, void* s) {
    SyPack* p = (SyPack*)pack;
    SyStr* a = (SyStr*)s;
    sy_pack_pon(p, 3);
    p->items[p->n - 1].v.s.len = a->len;
    p->items[p->n - 1].v.s.data = sy_strdup_n(a->data, a->len);
    return p;
}

long long sy_paquete_dame_int(void* pack, long long i) {
    return ((SyPack*)pack)->items[i].v.i;
}

double sy_paquete_dame_float(void* pack, long long i) {
    return ((SyPack*)pack)->items[i].v.d;
}

int sy_paquete_dame_bool(void* pack, long long i) {
    return (int)((SyPack*)pack)->items[i].v.i;
}

void* sy_paquete_dame_str(void* pack, long long i) {
    SyItem* it = &((SyPack*)pack)->items[i];
    return sy_str_new_n(it->v.s.data, it->v.s.len);
}

void sy_paquete_libera(void* pack) {
    SyPack* p = (SyPack*)pack;
    for (long long i = 0; i < p->n; i++)
        if (p->items[i].tag == 3) free(p->items[i].v.s.data);
    free(p->items);
    free(p);
}

#define SYL_MAX_HILOS 4096
static pthread_t sy_hilos[SYL_MAX_HILOS];
static int sy_hilo_vivo[SYL_MAX_HILOS];
static long long sy_hilo_n = 0;
static pthread_mutex_t sy_hilo_mx = PTHREAD_MUTEX_INITIALIZER;

long long sy_hilo_lanzar(void* (*fn)(void*), void* pack) {
    pthread_mutex_lock(&sy_hilo_mx);
    if (sy_hilo_n >= SYL_MAX_HILOS) {
        pthread_mutex_unlock(&sy_hilo_mx);
        sy_fatal("demasiados hilos abiertos sin esperar");
    }
    long long id = sy_hilo_n + 1;
    int rc = pthread_create(&sy_hilos[sy_hilo_n], NULL, fn, pack);
    if (rc != 0) {
        pthread_mutex_unlock(&sy_hilo_mx);
        sy_fatal("no puedo lanzar el hilo");
    }
    sy_hilo_vivo[sy_hilo_n] = 1;
    sy_hilo_n += 1;
    pthread_mutex_unlock(&sy_hilo_mx);
    return id;
}

void sy_hilo_esperar(long long id) {
    if (id < 1 || id > sy_hilo_n) return;
    pthread_mutex_lock(&sy_hilo_mx);
    int vivo = sy_hilo_vivo[id - 1];
    sy_hilo_vivo[id - 1] = 0;
    pthread_t t = sy_hilos[id - 1];
    pthread_mutex_unlock(&sy_hilo_mx);
    if (vivo) pthread_join(t, NULL);
}

void sy_hilo_dormir(long long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

jmp_buf sylt_jb;
static int sylt_armado = 0;
static char* sylt_fallo_msg = NULL;
static long long sylt_fallo_linea = 0;
static long long sylt_fallos = 0;

void sy_test_arma(void) {
    sylt_armado = 1;
    sylt_fallos = 0;
}

void sy_afirma(int ok, char* msg, long long linea) {
    if (ok) return;
    if (sylt_armado) {
        sylt_fallo_msg = msg;
        sylt_fallo_linea = linea;
        longjmp(sylt_jb, 1);
    }
    char buf[512];
    snprintf(buf, sizeof(buf), "afirmación fallida (línea %lld): %s", linea, msg);
    sy_fatal(buf);
}

void sy_test_ok(char* nombre) {
    printf("ok %s\n", nombre);
}

void sy_test_mal(char* nombre) {
    sylt_fallos += 1;
    printf("fallo: %s (línea %lld): %s\n", nombre, sylt_fallo_linea,
           sylt_fallo_msg ? sylt_fallo_msg : "?");
}

long long sy_test_resumen(long long total) {
    printf("%lld pruebas, %lld %s\n", total, sylt_fallos,
           sylt_fallos == 1 ? "fallo" : "fallos");
    return sylt_fallos;
}
