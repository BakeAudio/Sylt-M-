#pragma once
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
[[noreturn]] inline void SyltAbort(const std::string& Msg) {
    std::cerr << "error en ejecución: " << Msg << std::endl;
    std::exit(1);
}
inline std::string SyltStr(int64_t V) { return std::to_string(V); }
inline std::string SyltStr(bool V) { return V ? "true" : "false"; }
inline std::string SyltStr(double V) {
    if (std::isnan(V)) return "nan";
    if (std::isinf(V)) return V < 0 ? "-inf" : "inf";
    std::ostringstream O;
    O << V;
    std::string S = O.str();
    if (S.find('.') == std::string::npos && S.find('e') == std::string::npos)
        S += ".0";
    return S;
}
inline std::string SyltStr(const std::string& V) { return V; }
inline std::string SyltStr(const char* V) { return std::string(V); }
template <class T>
std::string SyltStr(const std::vector<T>& V) {
    std::string S = "[";
    bool F = true;
    for (auto& E : V) {
        if (!F) S += ", ";
        F = false;
        S += SyltStr(E);
    }
    return S + "]";
}
template <class K, class V>
std::string SyltStr(const std::unordered_map<K, V>& M) {
    std::vector<std::pair<K, V>> P(M.begin(), M.end());
    std::sort(P.begin(), P.end(),
              [](const std::pair<K, V>& A, const std::pair<K, V>& B) {
                  return A.first < B.first;
              });
    std::string S = "{";
    bool F = true;
    for (auto& KV : P) {
        if (!F) S += ", ";
        F = false;
        S += SyltStr(KV.first) + ": " + SyltStr(KV.second);
    }
    return S + "}";
}
template <class T>
void SyltTupleAdd(std::string& S, bool& F, const T& V) {
    if (!F) S += ", ";
    F = false;
    S += SyltStr(V);
}
template <class... Ts, size_t... Is>
std::string SyltTupleStr(const std::tuple<Ts...>& T, std::index_sequence<Is...>) {
    std::string S = "(";
    bool F = true;
    (void)std::initializer_list<int>{
        (SyltTupleAdd(S, F, std::get<Is>(T)), 0)...};
    return S + ")";
}
template <class... Ts>
std::string SyltStr(const std::tuple<Ts...>& T) {
    return SyltTupleStr(T, std::index_sequence_for<Ts...>{});
}
template <class T>
void SyltPrint(const T& V) {
    std::cout << SyltStr(V) << "\n";
}
template <class V, class T>
void SyltPush(V& Vec, const T& Val) {
    Vec.push_back(Val);
}
template <class V>
typename V::value_type SyltPop(V& Vec) {
    if (Vec.empty()) SyltAbort("Pop() sobre un array vacío");
    typename V::value_type Val = Vec.back();
    Vec.pop_back();
    return Val;
}
template <class V>
auto& SyltIdx(V& Vec, int64_t I, const char* Nombre) {
    int64_t N = (int64_t)Vec.size();
    if (I < 0 || I >= N)
        SyltAbort("índice " + SyltStr(I) + " fuera de rango en `" + Nombre +
                  "` (Len=" + SyltStr(N) + ")");
    return Vec[(size_t)I];
}
template <class V, class T>
bool SyltContieneVec(const V& Vec, const T& Val) {
    for (auto& E : Vec)
        if (E == Val) return true;
    return false;
}
template <class M>
auto& SyltMGet(M& Map, const typename M::key_type& K, const char* Nombre) {
    auto It = Map.find(K);
    if (It == Map.end())
        SyltAbort("la clave " + SyltStr(K) + " no existe en `" + Nombre + "`");
    return It->second;
}
template <class M>
int64_t SyltMGetCount(const M& Map, const typename M::key_type& K) {
    return (int64_t)Map.count(K);
}
template <class M>
std::vector<typename M::key_type> SyltClaves(const M& Map) {
    std::vector<typename M::key_type> R;
    for (auto& P : Map) R.push_back(P.first);
    std::sort(R.begin(), R.end());
    return R;
}
inline bool SyltContieneStr(const std::string& S, const std::string& Sub) {
    return S.find(Sub) != std::string::npos;
}
inline int64_t SyltDivI(int64_t A, int64_t B) {
    if (B == 0) SyltAbort("división entera por cero");
    return A / B;
}
inline int64_t SyltModI(int64_t A, int64_t B) {
    if (B == 0) SyltAbort("módulo entero por cero");
    return A % B;
}
inline double SyltSqrt(double X) { return std::sqrt(X); }
inline int64_t SyltAbs(int64_t X) { return X < 0 ? -X : X; }
inline double SyltAbs(double X) { return X < 0 ? -X : X; }
inline int64_t SyltMin(int64_t A, int64_t B) { return A < B ? A : B; }
inline int64_t SyltMax(int64_t A, int64_t B) { return A > B ? A : B; }
inline double SyltMin(double A, double B) { return A < B ? A : B; }
inline double SyltMax(double A, double B) { return A > B ? A : B; }
inline double SyltPow(double A, double B) { return std::pow(A, B); }
inline int64_t SyltFloor(double X) { return (int64_t)std::floor(X); }
template <class T>
T SyltClone(const T& X) {
    return X;
}
inline double SyltTiempoMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

inline std::vector<std::string> sy_str_dividir(const std::string& S, const std::string& Sep) {
    std::vector<std::string> R;
    if (Sep.empty()) {
        R.push_back(S);
        return R;
    }
    size_t Ini = 0;
    while (true) {
        size_t P = S.find(Sep, Ini);
        if (P == std::string::npos) {
            R.push_back(S.substr(Ini));
            break;
        }
        R.push_back(S.substr(Ini, P - Ini));
        Ini = P + Sep.size();
    }
    return R;
}
inline std::string sy_str_unir(const std::vector<std::string>& V, const std::string& Sep) {
    std::string R;
    for (size_t i = 0; i < V.size(); i++) {
        if (i) R += Sep;
        R += V[i];
    }
    return R;
}
inline std::string sy_str_recortar(const std::string& S) {
    size_t a = 0, b = S.size();
    while (a < b && (unsigned char)S[a] <= ' ') a++;
    while (b > a && (unsigned char)S[b - 1] <= ' ') b--;
    return S.substr(a, b - a);
}
inline std::string sy_str_mayusculas(const std::string& S) {
    std::string R = S;
    for (char& c : R) c = (char)std::toupper((unsigned char)c);
    return R;
}
inline std::string sy_str_minusculas(const std::string& S) {
    std::string R = S;
    for (char& c : R) c = (char)std::tolower((unsigned char)c);
    return R;
}
inline std::string sy_str_reemplazar(const std::string& S, const std::string& De,
                                     const std::string& A) {
    if (De.empty()) return S;
    std::string R = S;
    size_t P = 0;
    while ((P = R.find(De, P)) != std::string::npos) {
        R.replace(P, De.size(), A);
        P += A.size();
    }
    return R;
}
inline bool sy_str_empieza_con(const std::string& S, const std::string& P) {
    return S.size() >= P.size() && S.compare(0, P.size(), P) == 0;
}
inline bool sy_str_termina_con(const std::string& S, const std::string& P) {
    return S.size() >= P.size() && S.compare(S.size() - P.size(), P.size(), P) == 0;
}
inline int64_t sy_str_buscar(const std::string& S, const std::string& Sub) {
    size_t P = S.find(Sub);
    if (P == std::string::npos) return -1;
    return (int64_t)P;
}
inline std::string sy_str_repetir(const std::string& S, int64_t N) {
    if (N < 0) N = 0;
    if (N > 1000000) N = 1000000;
    std::string R;
    R.reserve(S.size() * (size_t)N);
    for (int64_t i = 0; i < N; i++) R += S;
    return R;
}
inline std::string sy_str_subcadena(const std::string& S, int64_t Ini, int64_t N) {
    if (Ini < 0) {
        Ini = (int64_t)S.size() + Ini;
        if (Ini < 0) Ini = 0;
    }
    if (Ini >= (int64_t)S.size()) return "";
    if (N < 0) N = 0;
    if (Ini + N > (int64_t)S.size()) N = (int64_t)S.size() - Ini;
    return S.substr((size_t)Ini, (size_t)N);
}
inline std::vector<std::string> sy_str_caracteres(const std::string& S) {
    std::vector<std::string> R;
    size_t i = 0;
    while (i < S.size()) {
        size_t L = 1;
        unsigned char c = (unsigned char)S[i];
        if (c >= 0xF0) L = 4;
        else if (c >= 0xE0) L = 3;
        else if (c >= 0xC0) L = 2;
        if (i + L > S.size()) L = S.size() - i;
        R.push_back(S.substr(i, L));
        i += L;
    }
    return R;
}
inline int64_t sy_str_a_entero(const std::string& S) {
    std::string T = S;
    size_t a = 0, b = T.size();
    while (a < b && (unsigned char)T[a] <= ' ') a++;
    while (b > a && (unsigned char)T[b - 1] <= ' ') b--;
    return (int64_t)strtoll(T.substr(a, b - a).c_str(), nullptr, 10);
}
inline std::string sy_io_leer_linea() {
    std::string L;
    if (!std::getline(std::cin, L)) return "";
    return L;
}
inline std::string sy_io_leer_archivo(const std::string& Ruta) {
    std::ifstream F(Ruta, std::ios::binary);
    if (!F) SyltAbort("no puedo leer el archivo `" + Ruta + "`");
    std::ostringstream S;
    S << F.rdbuf();
    return S.str();
}
inline void sy_io_escribir_archivo(const std::string& Ruta, const std::string& Contenido) {
    std::ofstream F(Ruta, std::ios::binary);
    if (!F) SyltAbort("no puedo escribir el archivo `" + Ruta + "`");
    F << Contenido;
}
inline void sy_io_anexar_archivo(const std::string& Ruta, const std::string& Linea) {
    std::ofstream F(Ruta, std::ios::app);
    if (!F) SyltAbort("no puedo anexar al archivo `" + Ruta + "`");
    F << Linea;
}
inline bool sy_io_existe(const std::string& Ruta) {
    std::ifstream F(Ruta);
    return F.good();
}
inline int64_t sy_time_epoch_ms() {
    using namespace std::chrono;
    return (int64_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
inline int64_t sy_net_conectar(const std::string& Host, int64_t Puerto) {
    addrinfo Hints{};
    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = SOCK_STREAM;
    addrinfo* Res = nullptr;
    if (getaddrinfo(Host.c_str(), std::to_string(Puerto).c_str(), &Hints, &Res) != 0 || !Res)
        return -1;
    int Fd = -1;
    for (addrinfo* P = Res; P; P = P->ai_next) {
        Fd = socket(P->ai_family, P->ai_socktype, P->ai_protocol);
        if (Fd < 0) continue;
        if (connect(Fd, P->ai_addr, P->ai_addrlen) == 0) break;
        close(Fd);
        Fd = -1;
    }
    freeaddrinfo(Res);
    return Fd;
}
inline void sy_net_enviar(int64_t Fd, const std::string& Datos) {
    size_t Enviado = 0;
    while (Enviado < Datos.size()) {
        ssize_t N = send((int)Fd, Datos.data() + Enviado, Datos.size() - Enviado, 0);
        if (N <= 0) SyltAbort("fallo al enviar por el socket");
        Enviado += (size_t)N;
    }
}
inline std::string sy_net_recibir(int64_t Fd, int64_t Max) {
    if (Max < 1) Max = 4096;
    if (Max > (1 << 20)) Max = (1 << 20);
    std::string Buf((size_t)Max, '\0');
    ssize_t N = recv((int)Fd, &Buf[0], (size_t)Max, 0);
    if (N < 0) N = 0;
    Buf.resize((size_t)N);
    return Buf;
}
inline void sy_net_cerrar(int64_t Fd) {
    close((int)Fd);
}
inline int64_t sy_net_escuchar(int64_t Puerto) {
    int Fd = socket(AF_INET, SOCK_STREAM, 0);
    if (Fd < 0) SyltAbort("no puedo crear el socket del servidor");
    int Uno = 1;
    setsockopt(Fd, SOL_SOCKET, SO_REUSEADDR, &Uno, sizeof(Uno));
    sockaddr_in Dir{};
    Dir.sin_family = AF_INET;
    Dir.sin_addr.s_addr = htonl(INADDR_ANY);
    Dir.sin_port = htons((uint16_t)Puerto);
    if (bind(Fd, (sockaddr*)&Dir, sizeof(Dir)) != 0)
        SyltAbort("no puedo enlazar el puerto " + SyltStr(Puerto) + " (¿está ocupado?)");
    if (listen(Fd, 16) != 0)
        SyltAbort("no puedo escuchar en el puerto " + SyltStr(Puerto));
    return Fd;
}
inline int64_t sy_net_aceptar(int64_t Fd) {
    int C = accept((int)Fd, nullptr, nullptr);
    if (C < 0) SyltAbort("fallo al aceptar una conexión");
    return C;
}
inline std::string sy_net_direccion(int64_t Fd) {
    sockaddr_in Dir{};
    socklen_t L = sizeof(Dir);
    if (getpeername((int)Fd, (sockaddr*)&Dir, &L) != 0) return "?";
    char Buf[64];
    std::snprintf(Buf, sizeof(Buf), "%s:%u",
                  inet_ntoa(Dir.sin_addr), (unsigned)ntohs(Dir.sin_port));
    return std::string(Buf);
}
