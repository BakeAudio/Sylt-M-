#include "fmt.hpp"
#include "json.hpp"
#include "loader.hpp"
#include "check.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <poll.h>
#include <set>
#include <unordered_map>
namespace sylt {
namespace {

std::string DecodificarUri(const std::string& uri) {
    std::string p = uri;
    if (StartsWith(p, "file://")) p = p.substr(7);
    std::string out;
    auto EsHex = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    };
    auto ValorHex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return c - 'A' + 10;
    };
    for (size_t i = 0; i < p.size(); i++) {
        if (p[i] == '%' && i + 2 < p.size() && EsHex(p[i + 1]) && EsHex(p[i + 2])) {
            out.push_back((char)(ValorHex(p[i + 1]) * 16 + ValorHex(p[i + 2])));
            i += 2;
        } else {
            out.push_back(p[i]);
        }
    }
    return out;
}

int TamSeq(unsigned char c) {
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

bool SeqValida(const std::string& s, size_t i, int cont) {
    if (i + (size_t)cont > s.size()) return false;
    for (int k = 1; k < cont; k++)
        if (((unsigned char)s[i + (size_t)k] & 0xC0) != 0x80) return false;
    return true;
}

uint32_t CpDe(const std::string& s, size_t i, int cont) {
    unsigned char c = (unsigned char)s[i];
    if (cont == 1) return c;
    uint32_t cp = c & (cont == 2 ? 0x1Fu : (cont == 3 ? 0x0Fu : 0x07u));
    for (int k = 1; k < cont; k++)
        cp = (cp << 6) | ((unsigned char)s[i + (size_t)k] & 0x3Fu);
    return cp;
}

int UnidadesDe(uint32_t cp) { return cp > 0xFFFF ? 2 : 1; }

int Utf16DeBytes(const std::string& linea, int byteCol) {
    if (byteCol < 1) byteCol = 1;
    size_t objetivo = (size_t)(byteCol - 1);
    size_t i = 0;
    int unidades = 0;
    while (i < linea.size() && i < objetivo) {
        int cont = TamSeq((unsigned char)linea[i]);
        uint32_t cp = 0xFFFD;
        if (SeqValida(linea, i, cont)) cp = CpDe(linea, i, cont);
        else cont = 1;
        unidades += UnidadesDe(cp);
        i += (size_t)cont;
    }
    return unidades;
}

int Utf16DeLen(const std::string& linea, int byteCol, int byteLen) {
    if (byteCol < 1) byteCol = 1;
    size_t ini = (size_t)(byteCol - 1);
    size_t fin = ini + (byteLen > 0 ? (size_t)byteLen : (size_t)1);
    if (fin > linea.size()) fin = linea.size();
    size_t i = ini;
    int unidades = 0;
    while (i < fin) {
        int cont = TamSeq((unsigned char)linea[i]);
        uint32_t cp = 0xFFFD;
        if (SeqValida(linea, i, cont)) cp = CpDe(linea, i, cont);
        else cont = 1;
        if (i + (size_t)cont > fin) break;
        unidades += UnidadesDe(cp);
        i += (size_t)cont;
    }
    return unidades;
}

int BytesDeUtf16(const std::string& linea, int col16) {
    size_t i = 0;
    int unidades = 0;
    while (i < linea.size() && unidades < col16) {
        int cont = TamSeq((unsigned char)linea[i]);
        uint32_t cp = 0xFFFD;
        if (SeqValida(linea, i, cont)) cp = CpDe(linea, i, cont);
        else cont = 1;
        int u = UnidadesDe(cp);
        if (unidades + u > col16) break;
        unidades += u;
        i += (size_t)cont;
    }
    return (int)i;
}

JsonValue Posicion(int linea0, int col0) {
    JsonValue p = JsonValue::MakeObj();
    p.Set("line", JsonValue::MakeInt(linea0));
    p.Set("character", JsonValue::MakeInt(col0));
    return p;
}

JsonValue PosicionFinal(const std::string& text) {
    if (text.empty()) return Posicion(0, 0);
    int lineas = 0;
    int ultima = 0;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\n') {
            lineas++;
            ultima = 0;
            i++;
            continue;
        }
        int cont = TamSeq((unsigned char)text[i]);
        uint32_t cp = 0xFFFD;
        if (SeqValida(text, i, cont)) cp = CpDe(text, i, cont);
        else cont = 1;
        ultima += UnidadesDe(cp);
        i += (size_t)cont;
    }
    if (lineas == 0) return Posicion(0, ultima);
    return Posicion(lineas, ultima);
}

std::vector<std::string> PartirLineas(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (text.empty() || text.back() != '\n') out.push_back(cur);
    return out;
}

struct ResultadoAnalisis {
    std::map<std::string, JsonValue> PorFichero;
    ProgramP Prog;
};

ResultadoAnalisis Analizar(const std::string& path, const std::string& text,
                           std::unordered_map<std::string, std::string>& overlays) {
    ResultadoAnalisis R;
    SourceFile src;
    src.Load(path, text);
    Reporter rep(src, false);
    std::vector<Diag> buf;
    rep.SetSink(&buf);
    Loader ld(rep, ResolveLibDir(""));
    ld.PkgDir = ResolvePkgDir("");
    ld.Overlays = &overlays;
    if (ld.Load(path, "__main__", false, Span())) {
        R.Prog = ld.Merge();
        if (rep.ErrorCount() == 0) {
            std::unordered_map<std::string, const SourceFile*> files;
            for (auto& U : ld.Units)
                files[U.Name == "__main__" ? "" : U.Name] = &U.Src;
            Check(R.Prog, src, rep, &files, false);
        }
    }
    for (const Diag& d : buf) {
        const SourceFile* F = d.File ? d.File : &src;
        const std::string& nombre = d.File ? d.File->Name : path;
        int ln = d.PrimaryValid ? d.Primary.Line : 1;
        int col = d.PrimaryValid ? d.Primary.Col : 1;
        int len = (d.PrimaryValid && d.Primary.Len > 0) ? d.Primary.Len : 1;
        if (ln < 1) ln = 1;
        if (col < 1) col = 1;
        std::string linea = F->LineAt(ln);
        int col16 = Utf16DeBytes(linea, col);
        int len16 = Utf16DeLen(linea, col, len);
        JsonValue rango = JsonValue::MakeObj();
        rango.Set("start", Posicion(ln - 1, col16));
        rango.Set("end", Posicion(ln - 1, col16 + len16));
        JsonValue dg = JsonValue::MakeObj();
        dg.Set("range", rango);
        dg.Set("severity", JsonValue::MakeInt(d.Severity == Diag::Sev::Error ? 1 : 2));
        dg.Set("code", JsonValue::MakeText(d.Code));
        dg.Set("source", JsonValue::MakeText("sylt"));
        std::string msg = d.Msg;
        for (const auto& n : d.Notes) msg += "\nnota: " + n.Text;
        for (const auto& h : d.Helps) msg += "\nayuda: " + h;
        dg.Set("message", JsonValue::MakeText(msg));
        auto it = R.PorFichero.find(nombre);
        if (it == R.PorFichero.end()) it = R.PorFichero.emplace(nombre, JsonValue::MakeArr()).first;
        it->second.Push(dg);
    }
    return R;
}

struct DocBreve {
    const char* Palabra;
    const char* Doc;
};

const std::vector<DocBreve> kPalabrasClave = {
    {"fn", "define una función: `fn Suma(A: int, B: int) -> int:`"},
    {"let", "declara una variable inmutable: `let X = 5`"},
    {"var", "declara una variable mutable: `var N = 0`"},
    {"type", "define un struct con campos: `type Punto:`"},
    {"if", "condicional: `if X > 0:` con `elif` y `else`"},
    {"elif", "rama alternativa de un `if`"},
    {"else", "rama final de un `if` o un `match`"},
    {"while", "bucle mientras la condición sea cierta"},
    {"for", "bucle sobre un rango o una colección: `for I in 0..N:`"},
    {"in", "iteración del `for` y pertenencia"},
    {"return", "devuelve un valor de la función"},
    {"break", "sale del bucle actual"},
    {"continue", "pasa a la siguiente vuelta del bucle"},
    {"pass", "sentencia vacía para bloques todavía sin código"},
    {"and", "conjunción lógica: `if A and B`"},
    {"or", "disyunción lógica: `if A or B`"},
    {"not", "negación lógica: `not Visible`"},
    {"true", "valor booleano verdadero"},
    {"false", "valor booleano falso"},
    {"self", "la instancia actual dentro de un método"},
    {"mut", "marca un parámetro o un receptor como mutable"},
    {"match", "coincidencia por variantes: `match X:` con `case`"},
    {"case", "rama de un `match`"},
    {"import", "importa un módulo: `import str` o `import helpers`"},
    {"foreign", "declara una función externa en C: `foreign fn Nombre(...)`"},
    {"enum", "define un enum con variantes"},
};

const std::vector<DocBreve> kIntegradas = {
    {"Print", "Print(valor) — escribe un valor en la salida estándar"},
    {"Str", "Str(x) o Str(x, decimales) — convierte a texto"},
    {"Sqrt", "Sqrt(x) — raíz cuadrada, devuelve float"},
    {"Abs", "Abs(x) — valor absoluto"},
    {"Min", "Min(a, b) — el menor de dos números"},
    {"Max", "Max(a, b) — el mayor de dos números"},
    {"Pow", "Pow(base, exponente) — potencia"},
    {"Floor", "Floor(x) — redondeo hacia abajo"},
    {"Clone", "Clone(struct) — copia profunda para conservar el original"},
    {"TiempoMs", "TiempoMs() — milisegundos desde un origen fijo"},
    {"Afirma", "Afirma(cond, \"mensaje\") — rompe la ejecución si la condición falla"},
    {"HiloLanzar", "HiloLanzar(fn, args...) — lanza una función en un hilo"},
};

bool EsLetraDePalabra(char c) {
    return isalnum((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80;
}

std::string TextoTy(const TypeAnn& t) {
    switch (t.k) {
        case TypeAnn::K::None: return t.Name;
        case TypeAnn::K::Prim: return t.Name;
        case TypeAnn::K::Ident: return t.Name;
        case TypeAnn::K::Array: return "[" + TextoTy(t.Sub[0]) + "]";
        case TypeAnn::K::Map:
            return "{" + (t.Sub.empty() ? "" : TextoTy(t.Sub[0])) + ": " +
                   (t.Sub.size() > 1 ? TextoTy(t.Sub[1]) : "") + "}";
        case TypeAnn::K::Tuple: {
            std::string s = "(";
            for (size_t i = 0; i < t.Sub.size(); i++) {
                if (i) s += ", ";
                s += TextoTy(t.Sub[i]);
            }
            return s + ")";
        }
        case TypeAnn::K::Borrow:
            return t.Mut ? "&mut " + (t.Sub.empty() ? std::string() : TextoTy(t.Sub[0]))
                         : "&" + (t.Sub.empty() ? std::string() : TextoTy(t.Sub[0]));
        case TypeAnn::K::Generic: {
            std::string s = t.Name + "<";
            for (size_t i = 0; i < t.Sub.size(); i++) {
                if (i) s += ", ";
                s += TextoTy(t.Sub[i]);
            }
            return s + ">";
        }
    }
    return t.Name;
}

std::string FirmaFn(const FnDecl& fd) {
    std::string s = "fn " + fd.Name + "(";
    for (size_t i = 0; i < fd.Params.size(); i++) {
        if (i) s += ", ";
        if (fd.Params[i].IsSelf)
            s += fd.Params[i].Mut ? "mut self" : "self";
        else
            s += fd.Params[i].Name +
                 (fd.Params[i].HasTy ? ": " + TextoTy(fd.Params[i].Ty) : "");
    }
    s += ")";
    if (fd.HasRet) s += " -> " + TextoTy(fd.Ret);
    return s;
}

std::string BloqueCodigo(const std::string& codigo, const std::string& extra) {
    std::string s = "```sylt\n" + codigo + "\n```";
    if (!extra.empty()) s += "\n\n" + extra;
    return s;
}

enum class Frame { Ok, Malo, Eof };

Frame LeerMensaje(std::string& out, std::string& porque, bool& resincronizando) {
    std::string linea;
    size_t longitud = 0;
    bool vioLongitud = false;
    bool valorMalo = false;
    if (resincronizando) {
        while (true) {
            if (!std::getline(std::cin, linea)) return Frame::Eof;
            if (!linea.empty() && linea.back() == '\r') linea.pop_back();
            size_t posC = linea.find("Content-Length:");
            if (posC != std::string::npos) {
                std::string v = Trim(linea.substr(posC + 15));
                if (!v.empty() && v.find_first_not_of("0123456789") == std::string::npos) {
                    longitud = (size_t)strtoull(v.c_str(), nullptr, 10);
                    vioLongitud = true;
                    resincronizando = false;
                    break;
                }
            }
        }
    }
    while (true) {
        if (!std::getline(std::cin, linea)) return Frame::Eof;
        if (!linea.empty() && linea.back() == '\r') linea.pop_back();
        if (linea.empty()) break;
        size_t posC = linea.find("Content-Length:");
        if (posC != std::string::npos) {
            std::string v = Trim(linea.substr(posC + 15));
            if (!v.empty() && v.find_first_not_of("0123456789") == std::string::npos) {
                longitud = (size_t)strtoull(v.c_str(), nullptr, 10);
                vioLongitud = true;
                valorMalo = false;
            } else {
                valorMalo = true;
            }
        }
    }
    if (!vioLongitud) {
        porque = valorMalo ? "la cabecera Content-Length no trae un número válido"
                           : "falta la cabecera Content-Length";
        resincronizando = true;
        return Frame::Malo;
    }
    if (longitud == 0) {
        porque = "Content-Length es 0";
        return Frame::Malo;
    }
    if (longitud > 64u * 1024 * 1024) {
        char Basura[65536];
        size_t queda = longitud;
        while (queda > 0) {
            size_t trozo = queda < sizeof(Basura) ? queda : sizeof(Basura);
            std::cin.read(Basura, (std::streamsize)trozo);
            if ((size_t)std::cin.gcount() != trozo) return Frame::Eof;
            queda -= trozo;
        }
        porque = "el mensaje supera el límite de 64 MiB";
        return Frame::Malo;
    }
    std::string cuerpo(longitud, '\0');
    std::cin.read(&cuerpo[0], (std::streamsize)longitud);
    if ((size_t)std::cin.gcount() != longitud) return Frame::Eof;
    out = std::move(cuerpo);
    return Frame::Ok;
}

bool HayEntrada(int ms) {
    if (std::cin.rdbuf()->in_avail() > 0) return true;
    struct pollfd P;
    P.fd = 0;
    P.events = POLLIN;
    P.revents = 0;
    int r = poll(&P, 1, ms);
    return r > 0 && (P.revents & (POLLIN | POLLHUP)) != 0;
}

struct ServidorLsp {
    std::unordered_map<std::string, std::string> Documentos;
    std::unordered_map<std::string, std::string> Overlays;
    bool ApagadoRecibido = false;
    bool Pendiente = false;
    std::string UriPendiente;
    std::set<std::string> UrisConDiags;
    ProgramP UltimoProg;
    std::string UltimaUri;

    void Enviar(const std::string& cuerpo) {
        std::cout << "Content-Length: " << cuerpo.size() << "\r\n\r\n" << cuerpo;
        std::cout.flush();
    }
    void EnviarJson(const JsonValue& v) { Enviar(JsonDump(v)); }
    void Responder(const JsonValue& id, JsonValue resultado) {
        JsonValue r = JsonValue::MakeObj();
        r.Set("jsonrpc", JsonValue::MakeText("2.0"));
        r.Set("id", id);
        r.Set("result", std::move(resultado));
        EnviarJson(r);
    }
    void ResponderError(const JsonValue& id, int codigo, const std::string& msg) {
        JsonValue e = JsonValue::MakeObj();
        e.Set("code", JsonValue::MakeInt(codigo));
        e.Set("message", JsonValue::MakeText(msg));
        JsonValue r = JsonValue::MakeObj();
        r.Set("jsonrpc", JsonValue::MakeText("2.0"));
        r.Set("id", id);
        r.Set("error", std::move(e));
        EnviarJson(r);
    }
    void Notificar(const std::string& metodo, const JsonValue& params) {
        JsonValue r = JsonValue::MakeObj();
        r.Set("jsonrpc", JsonValue::MakeText("2.0"));
        r.Set("method", JsonValue::MakeText(metodo));
        r.Set("params", params);
        EnviarJson(r);
    }
    void PublicarDiags(const std::string& uri, JsonValue diags) {
        JsonValue params = JsonValue::MakeObj();
        params.Set("uri", JsonValue::MakeText(uri));
        params.Set("diagnostics", std::move(diags));
        Notificar("textDocument/publishDiagnostics", params);
    }
    void ErrorProtocolo(const std::string& porque) {
        JsonValue e = JsonValue::MakeObj();
        e.Set("code", JsonValue::MakeInt(-32600));
        e.Set("message", JsonValue::MakeText("mensaje no válido: " + porque));
        JsonValue r = JsonValue::MakeObj();
        r.Set("jsonrpc", JsonValue::MakeText("2.0"));
        r.Set("id", JsonValue::MakeNull());
        r.Set("error", std::move(e));
        EnviarJson(r);
    }
    std::string TextoDe(const std::string& uri) {
        auto it = Documentos.find(uri);
        if (it != Documentos.end()) return it->second;
        std::string text;
        ReadFile(DecodificarUri(uri), text);
        return text;
    }
    void AnalizarYPublicar(const std::string& uri) {
        std::string path = DecodificarUri(uri);
        std::string text = TextoDe(uri);
        Overlays.clear();
        for (auto& kv : Documentos) Overlays[DecodificarUri(kv.first)] = kv.second;
        if (!Overlays.count(path)) Overlays[path] = text;
        ResultadoAnalisis R = Analizar(path, text, Overlays);
        if (!R.PorFichero.count(path)) R.PorFichero.emplace(path, JsonValue::MakeArr());
        for (auto& kv : Documentos) {
            std::string p2 = DecodificarUri(kv.first);
            if (!R.PorFichero.count(p2)) R.PorFichero.emplace(p2, JsonValue::MakeArr());
        }
        std::set<std::string> publicados;
        for (auto& kv : R.PorFichero) {
            std::string uriFichero = (kv.first == path) ? uri : "file://" + kv.first;
            PublicarDiags(uriFichero, kv.second);
            publicados.insert(uriFichero);
        }
        for (const std::string& previa : UrisConDiags) {
            if (!publicados.count(previa)) PublicarDiags(previa, JsonValue::MakeArr());
        }
        UrisConDiags = std::move(publicados);
        UltimoProg = std::move(R.Prog);
        UltimaUri = uri;
    }
    void AsegurarPrograma(const std::string& uri) {
        if (UltimaUri == uri && UltimoProg) return;
        std::string path = DecodificarUri(uri);
        std::string text = TextoDe(uri);
        Overlays.clear();
        for (auto& kv : Documentos) Overlays[DecodificarUri(kv.first)] = kv.second;
        if (!Overlays.count(path)) Overlays[path] = text;
        ResultadoAnalisis R = Analizar(path, text, Overlays);
        UltimoProg = std::move(R.Prog);
        UltimaUri = uri;
    }
    void Flujo() {
        if (!Pendiente) return;
        Pendiente = false;
        AnalizarYPublicar(UriPendiente);
    }
    void Abrir(const JsonValue& params) {
        const JsonValue* td = params.At("textDocument");
        if (!td) return;
        const JsonValue* uri = td->At("uri");
        const JsonValue* text = td->At("text");
        if (!uri || !text || !uri->IsText()) return;
        Documentos[uri->AsText()] = text->AsText();
        UriPendiente = uri->AsText();
        Pendiente = true;
    }
    void Cambiar(const JsonValue& params) {
        const JsonValue* td = params.At("textDocument");
        if (!td) return;
        const JsonValue* uri = td->At("uri");
        if (!uri || !uri->IsText()) return;
        const JsonValue* cambios = params.At("contentChanges");
        if (!cambios || !cambios->IsArr() || cambios->A.empty()) return;
        const JsonValue* ultimo = &cambios->A.back();
        const JsonValue* text = ultimo->At("text");
        if (!text || !text->IsText()) return;
        Documentos[uri->AsText()] = text->AsText();
        UriPendiente = uri->AsText();
        Pendiente = true;
    }
    void Cerrar(const JsonValue& params) {
        const JsonValue* td = params.At("textDocument");
        if (!td) return;
        const JsonValue* uri = td->At("uri");
        if (!uri || !uri->IsText()) return;
        Documentos.erase(uri->AsText());
        UrisConDiags.erase(uri->AsText());
        JsonValue vacio = JsonValue::MakeArr();
        PublicarDiags(uri->AsText(), std::move(vacio));
    }
    void Formatear(const JsonValue& id, const JsonValue& params) {
        const JsonValue* td = params.At("textDocument");
        if (!td) {
            ResponderError(id, -32602, "falta textDocument");
            return;
        }
        const JsonValue* uri = td->At("uri");
        if (!uri || !uri->IsText()) {
            ResponderError(id, -32602, "falta uri");
            return;
        }
        std::string text = TextoDe(uri->AsText());
        std::string out;
        std::vector<Diag> buf;
        JsonValue edits = JsonValue::MakeArr();
        if (FormatSource(DecodificarUri(uri->AsText()), text, out, &buf) && out != text) {
            JsonValue edit = JsonValue::MakeObj();
            JsonValue rango = JsonValue::MakeObj();
            rango.Set("start", Posicion(0, 0));
            rango.Set("end", PosicionFinal(text));
            edit.Set("range", rango);
            edit.Set("newText", JsonValue::MakeText(out));
            edits.Push(edit);
        }
        Responder(id, std::move(edits));
    }
    std::string NombreModuloCompleto(const std::string& base) {
        if (!UltimoProg) return "";
        for (auto& im : UltimoProg->Imports) {
            if (im.Module == base) return im.Module;
            if (im.Module.size() > base.size() &&
                im.Module.compare(im.Module.size() - base.size(), base.size(), base) == 0 &&
                im.Module[im.Module.size() - base.size() - 1] == '.')
                return im.Module;
        }
        return "";
    }
    std::string BuscarDoc(const std::string& palabra) {
        for (auto& k : kPalabrasClave) {
            if (palabra == k.Palabra) {
                return std::string("**`") + k.Palabra + "`** — " + k.Doc;
            }
        }
        for (auto& b : kIntegradas) {
            if (palabra == b.Palabra) {
                return BloqueCodigo(std::string(b.Palabra) + "(...)",
                                    "función integrada: " + std::string(b.Doc));
            }
        }
        if (!UltimoProg) return "";
        for (auto& fd : UltimoProg->Fns) {
            if (fd->Name != palabra || fd->IsMethod) continue;
            std::string donde = fd->Module.empty()
                                    ? "función del programa"
                                    : "función del módulo `" + fd->Module + "`";
            return BloqueCodigo(FirmaFn(*fd), donde);
        }
        for (auto& td : UltimoProg->Types) {
            if (td->Name != palabra) continue;
            std::string cuerpo = "type " + td->Name;
            std::string campos;
            for (auto& f : td->Fields) {
                campos += "\n- " + f.Name + (f.Ty.k != TypeAnn::K::None
                                                 ? ": " + TextoTy(f.Ty)
                                                 : "");
            }
            for (auto& m : td->Methods) campos += "\n- " + FirmaFn(*m);
            std::string donde = td->Module.empty()
                                    ? ""
                                    : "\n\ndeclarado en el módulo `" + td->Module + "`";
            return BloqueCodigo(cuerpo, campos.empty() ? donde : campos + donde);
        }
        for (auto& ed : UltimoProg->Enums) {
            if (ed->Name != palabra) continue;
            std::string cuerpo = "enum " + ed->Name;
            std::string vars;
            for (auto& v : ed->Variants) {
                vars += "\n- " + v.Name;
                for (auto& f : v.Fields) {
                    vars += (f.Ty.k != TypeAnn::K::None ? ": " + TextoTy(f.Ty) : "");
                }
            }
            std::string donde = ed->Module.empty()
                                    ? ""
                                    : "\n\ndeclarado en el módulo `" + ed->Module + "`";
            return BloqueCodigo(cuerpo, vars.empty() ? donde : vars + donde);
        }
        for (auto& im : UltimoProg->Imports) {
            if (im.Module == palabra) {
                return BloqueCodigo("import " + palabra, "módulo importado");
            }
        }
        return "";
    }
    void Hoverar(const JsonValue& id, const JsonValue& params) {
        const JsonValue* td = params.At("textDocument");
        const JsonValue* pos = params.At("position");
        const JsonValue* uriV = td ? td->At("uri") : nullptr;
        const JsonValue* lineaV = pos ? pos->At("line") : nullptr;
        const JsonValue* colV = pos ? pos->At("character") : nullptr;
        if (!uriV || !uriV->IsText() || !lineaV || !colV) {
            Responder(id, JsonValue::MakeNull());
            return;
        }
        AsegurarPrograma(uriV->AsText());
        std::string text = TextoDe(uriV->AsText());
        std::vector<std::string> lineas = PartirLineas(text);
        int linea0 = (int)lineaV->AsInt();
        int col16 = (int)colV->AsInt();
        if (linea0 < 0 || linea0 >= (int)lineas.size()) {
            Responder(id, JsonValue::MakeNull());
            return;
        }
        const std::string& linea = lineas[(size_t)linea0];
        int byteCol = BytesDeUtf16(linea, col16);
        size_t ini = (size_t)byteCol;
        size_t fin = ini;
        while (ini > 0 && EsLetraDePalabra(linea[ini - 1])) ini--;
        while (fin < linea.size() && EsLetraDePalabra(linea[fin])) fin++;
        if (ini >= fin) {
            Responder(id, JsonValue::MakeNull());
            return;
        }
        std::string palabra = linea.substr(ini, fin - ini);
        std::string doc = BuscarDoc(palabra);
        if (doc.empty()) {
            Responder(id, JsonValue::MakeNull());
            return;
        }
        JsonValue marca = JsonValue::MakeObj();
        marca.Set("kind", JsonValue::MakeText("markdown"));
        marca.Set("value", JsonValue::MakeText(doc));
        JsonValue res = JsonValue::MakeObj();
        res.Set("contents", std::move(marca));
        JsonValue rango = JsonValue::MakeObj();
        rango.Set("start", Posicion(linea0, Utf16DeBytes(linea, (int)ini + 1)));
        rango.Set("end", Posicion(linea0, Utf16DeBytes(linea, (int)fin + 1)));
        res.Set("range", std::move(rango));
        Responder(id, std::move(res));
    }
    void ItemCompletar(JsonValue& arr, const std::string& etiqueta, int kind,
                       const std::string& detalle) {
        JsonValue item = JsonValue::MakeObj();
        item.Set("label", JsonValue::MakeText(etiqueta));
        item.Set("kind", JsonValue::MakeInt(kind));
        if (!detalle.empty()) item.Set("detail", JsonValue::MakeText(detalle));
        arr.Push(item);
    }
    void Completar(const JsonValue& id, const JsonValue& params) {
        JsonValue vacio = JsonValue::MakeArr();
        const JsonValue* td = params.At("textDocument");
        const JsonValue* pos = params.At("position");
        const JsonValue* uriV = td ? td->At("uri") : nullptr;
        const JsonValue* lineaV = pos ? pos->At("line") : nullptr;
        const JsonValue* colV = pos ? pos->At("character") : nullptr;
        if (!uriV || !uriV->IsText() || !lineaV || !colV) {
            Responder(id, std::move(vacio));
            return;
        }
        AsegurarPrograma(uriV->AsText());
        std::string text = TextoDe(uriV->AsText());
        std::vector<std::string> lineas = PartirLineas(text);
        int linea0 = (int)lineaV->AsInt();
        int col16 = (int)colV->AsInt();
        if (linea0 < 0 || linea0 >= (int)lineas.size()) {
            Responder(id, std::move(vacio));
            return;
        }
        const std::string& linea = lineas[(size_t)linea0];
        int byteCol = BytesDeUtf16(linea, col16);
        size_t p = (size_t)byteCol;
        while (p > 0 && EsLetraDePalabra(linea[p - 1])) p--;
        JsonValue arr = JsonValue::MakeArr();
        if (p > 0 && linea[p - 1] == '.') {
            size_t q = p - 1;
            size_t finBase = q;
            while (q > 0 && EsLetraDePalabra(linea[q - 1])) q--;
            std::string base = linea.substr(q, finBase - q);
            std::string completo = NombreModuloCompleto(base);
            if (completo.empty() || !UltimoProg) {
                Responder(id, std::move(arr));
                return;
            }
            for (auto& fd : UltimoProg->Fns) {
                if (fd->Module != completo || fd->IsMethod) continue;
                ItemCompletar(arr, fd->Name, 3, FirmaFn(*fd));
            }
            for (auto& td : UltimoProg->Types) {
                if (td->Module != completo) continue;
                ItemCompletar(arr, td->Name, 22, "type " + td->Name);
            }
            for (auto& ed : UltimoProg->Enums) {
                if (ed->Module != completo) continue;
                ItemCompletar(arr, ed->Name, 13, "enum " + ed->Name);
            }
            Responder(id, std::move(arr));
            return;
        }
        for (auto& k : kPalabrasClave)
            ItemCompletar(arr, k.Palabra, 14, "palabra clave");
        for (auto& b : kIntegradas)
            ItemCompletar(arr, b.Palabra, 3, std::string(b.Doc));
        if (UltimoProg) {
            for (auto& fd : UltimoProg->Fns) {
                if (fd->IsMethod) continue;
                ItemCompletar(arr, fd->Name, 3, FirmaFn(*fd));
            }
            for (auto& td : UltimoProg->Types)
                ItemCompletar(arr, td->Name, 22, "type " + td->Name);
            for (auto& ed : UltimoProg->Enums)
                ItemCompletar(arr, ed->Name, 13, "enum " + ed->Name);
            for (auto& im : UltimoProg->Imports)
                ItemCompletar(arr, im.Module, 9, "módulo");
        }
        Responder(id, std::move(arr));
    }
    bool Despachar(const JsonValue& msg) {
        const JsonValue* metodo = msg.At("method");
        if (!metodo || !metodo->IsText()) return Pendiente;
        const JsonValue* idv = msg.At("id");
        const JsonValue* params = msg.At("params");
        JsonValue vacio = JsonValue::MakeObj();
        const JsonValue& P = params ? *params : vacio;
        const std::string& m = metodo->AsText();
        if (m == "initialize") {
            JsonValue caps = JsonValue::MakeObj();
            caps.Set("textDocumentSync", JsonValue::MakeInt(1));
            caps.Set("documentFormattingProvider", JsonValue::MakeBool(true));
            caps.Set("hoverProvider", JsonValue::MakeBool(true));
            JsonValue comp = JsonValue::MakeObj();
            comp.Set("resolveProvider", JsonValue::MakeBool(false));
            JsonValue disparadores = JsonValue::MakeArr();
            disparadores.Push(JsonValue::MakeText("."));
            comp.Set("triggerCharacters", std::move(disparadores));
            caps.Set("completionProvider", std::move(comp));
            caps.Set("positionEncoding", JsonValue::MakeText("utf-16"));
            JsonValue info = JsonValue::MakeObj();
            info.Set("name", JsonValue::MakeText("sylt"));
            info.Set("version", JsonValue::MakeText("0.7.3"));
            JsonValue res = JsonValue::MakeObj();
            res.Set("capabilities", caps);
            res.Set("serverInfo", info);
            Responder(idv ? *idv : JsonValue::MakeNull(), std::move(res));
            return Pendiente;
        }
        if (m == "initialized") return Pendiente;
        if (m == "shutdown") {
            ApagadoRecibido = true;
            Responder(idv ? *idv : JsonValue::MakeNull(), JsonValue::MakeNull());
            return Pendiente;
        }
        if (m == "exit") std::exit(ApagadoRecibido ? 0 : 1);
        if (m == "textDocument/didOpen") {
            Abrir(P);
            return Pendiente;
        }
        if (m == "textDocument/didChange") {
            Cambiar(P);
            return Pendiente;
        }
        if (m == "textDocument/didClose") {
            Cerrar(P);
            return Pendiente;
        }
        if (m == "textDocument/formatting") {
            if (idv) Formatear(*idv, P);
            return Pendiente;
        }
        if (m == "textDocument/hover") {
            if (idv) Hoverar(*idv, P);
            return Pendiente;
        }
        if (m == "textDocument/completion") {
            if (idv) Completar(*idv, P);
            return Pendiente;
        }
        if (StartsWith(m, "$/")) return Pendiente;
        if (m == "workspace/didChangeConfiguration" ||
            m == "workspace/didChangeWatchedFiles")
            return Pendiente;
        if (idv) ResponderError(*idv, -32601, "método no soportado: " + m);
        return Pendiente;
    }
};

void DespachoSeguro(ServidorLsp& S, const JsonValue& msg) {
    try {
        S.Despachar(msg);
    } catch (const std::exception& E) {
        const JsonValue* idv = msg.At("id");
        S.ResponderError(idv ? *idv : JsonValue::MakeNull(), -32603,
                         std::string("error interno del servidor: ") + E.what());
    } catch (...) {
        const JsonValue* idv = msg.At("id");
        S.ResponderError(idv ? *idv : JsonValue::MakeNull(), -32603,
                         "error interno del servidor");
    }
}

void FlujoSeguro(ServidorLsp& S) {
    try {
        S.Flujo();
    } catch (...) {
    }
}
}
int RunLsp() {
    std::ios::sync_with_stdio(false);
    ServidorLsp S;
    bool Resinc = false;
    while (true) {
        std::string cuerpo;
        std::string porque;
        Frame f = LeerMensaje(cuerpo, porque, Resinc);
        if (f == Frame::Eof) return 0;
        if (f == Frame::Malo) {
            S.ErrorProtocolo(porque);
            continue;
        }
        JsonValue msg;
        std::string err;
        if (!JsonParse(cuerpo, msg, err)) {
            JsonValue e = JsonValue::MakeObj();
            e.Set("code", JsonValue::MakeInt(-32700));
            e.Set("message", JsonValue::MakeText("JSON mal formado: " + err));
            JsonValue r = JsonValue::MakeObj();
            r.Set("jsonrpc", JsonValue::MakeText("2.0"));
            r.Set("id", JsonValue::MakeNull());
            r.Set("error", std::move(e));
            S.EnviarJson(r);
            continue;
        }
        DespachoSeguro(S, msg);
        while (S.Pendiente && HayEntrada(300)) {
            f = LeerMensaje(cuerpo, porque, Resinc);
            if (f == Frame::Eof) {
                FlujoSeguro(S);
                return 0;
            }
            if (f == Frame::Malo) {
                S.ErrorProtocolo(porque);
                continue;
            }
            if (!JsonParse(cuerpo, msg, err)) {
                JsonValue e = JsonValue::MakeObj();
                e.Set("code", JsonValue::MakeInt(-32700));
                e.Set("message", JsonValue::MakeText("JSON mal formado: " + err));
                JsonValue r = JsonValue::MakeObj();
                r.Set("jsonrpc", JsonValue::MakeText("2.0"));
                r.Set("id", JsonValue::MakeNull());
                r.Set("error", std::move(e));
                S.EnviarJson(r);
                continue;
            }
            DespachoSeguro(S, msg);
        }
        FlujoSeguro(S);
    }
}
}
