#include "json.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace sylt {
namespace {
const int ProfundidadMaxima = 128;

void PonerUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

struct LectorJson {
    const std::string& S;
    size_t P = 0;
    int Profundidad = 0;
    std::string Err;
    explicit LectorJson(const std::string& s) : S(s) {}
    bool Fin() const { return P >= S.size(); }
    char Cur() const { return P < S.size() ? S[P] : '\0'; }
    void Avanzar() { P++; }
    void SaltarEspacios() {
        while (!Fin() && (Cur() == ' ' || Cur() == '\t' || Cur() == '\n' || Cur() == '\r'))
            Avanzar();
    }
    bool Fallo(const std::string& msg) {
        if (Err.empty()) {
            Err = "error de sintaxis en la posición " + std::to_string(P) + ": " + msg;
        }
        return false;
    }
    bool LeerHex4(unsigned int& v) {
        v = 0;
        for (int i = 0; i < 4; i++) {
            char c = Cur();
            unsigned int d;
            if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
            else return Fallo("esperaba cuatro dígitos hexadecimales tras \\u");
            v = v * 16 + d;
            Avanzar();
        }
        return true;
    }
    bool LeerCadena(std::string& out) {
        if (Cur() != '"') return Fallo("esperaba una cadena entre comillas");
        Avanzar();
        out.clear();
        while (true) {
            if (Fin()) return Fallo("cadena sin cerrar");
            char c = Cur();
            if (c == '"') {
                Avanzar();
                return true;
            }
            if (c == '\\') {
                Avanzar();
                if (Fin()) return Fallo("escape sin carácter al final de la cadena");
                char e = Cur();
                if (e == '"') { out.push_back('"'); Avanzar(); }
                else if (e == '\\') { out.push_back('\\'); Avanzar(); }
                else if (e == '/') { out.push_back('/'); Avanzar(); }
                else if (e == 'b') { out.push_back('\b'); Avanzar(); }
                else if (e == 'f') { out.push_back('\f'); Avanzar(); }
                else if (e == 'n') { out.push_back('\n'); Avanzar(); }
                else if (e == 'r') { out.push_back('\r'); Avanzar(); }
                else if (e == 't') { out.push_back('\t'); Avanzar(); }
                else if (e == 'u') {
                    Avanzar();
                    unsigned int cp;
                    if (!LeerHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (Cur() != '\\' || P + 1 >= S.size() || S[P + 1] != 'u')
                            return Fallo("surrogado alto sin su pareja baja");
                        Avanzar();
                        Avanzar();
                        unsigned int low;
                        if (!LeerHex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF)
                            return Fallo("surrogado bajo fuera de rango");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return Fallo("surrogado bajo sin su pareja alta");
                    }
                    PonerUtf8(out, cp);
                } else {
                    return Fallo(std::string("escape desconocido \\") + e);
                }
                continue;
            }
            if ((unsigned char)c < 0x20) return Fallo("carácter de control sin escapar");
            out.push_back(c);
            Avanzar();
        }
    }
    bool LeerNumero(double& v) {
        size_t ini = P;
        if (Cur() == '-') Avanzar();
        if (Fin() || Cur() < '0' || Cur() > '9') return Fallo("número mal formado");
        while (!Fin() && Cur() >= '0' && Cur() <= '9') Avanzar();
        if (Cur() == '.') {
            Avanzar();
            if (Fin() || Cur() < '0' || Cur() > '9') return Fallo("falta el decimal tras el punto");
            while (!Fin() && Cur() >= '0' && Cur() <= '9') Avanzar();
        }
        if (Cur() == 'e' || Cur() == 'E') {
            Avanzar();
            if (Cur() == '+' || Cur() == '-') Avanzar();
            if (Fin() || Cur() < '0' || Cur() > '9') return Fallo("exponente sin dígitos");
            while (!Fin() && Cur() >= '0' && Cur() <= '9') Avanzar();
        }
        v = strtod(S.substr(ini, P - ini).c_str(), nullptr);
        return true;
    }
    bool LeerValor(JsonValue& out) {
        if (Profundidad >= ProfundidadMaxima) return Fallo("demasiados niveles de anidación");
        SaltarEspacios();
        if (Fin()) return Fallo("entrada terminada de forma inesperada");
        char c = Cur();
        if (c == '{') {
            Avanzar();
            out = JsonValue::MakeObj();
            Profundidad++;
            SaltarEspacios();
            if (Cur() == '}') {
                Avanzar();
                Profundidad--;
                return true;
            }
            while (true) {
                SaltarEspacios();
                std::string clave;
                if (!LeerCadena(clave)) return false;
                SaltarEspacios();
                if (Cur() != ':') return Fallo("esperaba ':' entre clave y valor");
                Avanzar();
                JsonValue v;
                if (!LeerValor(v)) return false;
                out.O[clave] = std::move(v);
                SaltarEspacios();
                if (Cur() == ',') {
                    Avanzar();
                    continue;
                }
                if (Cur() == '}') {
                    Avanzar();
                    Profundidad--;
                    return true;
                }
                return Fallo("esperaba ',' o '}' en el objeto");
            }
        }
        if (c == '[') {
            Avanzar();
            out = JsonValue::MakeArr();
            Profundidad++;
            SaltarEspacios();
            if (Cur() == ']') {
                Avanzar();
                Profundidad--;
                return true;
            }
            while (true) {
                JsonValue v;
                if (!LeerValor(v)) return false;
                out.A.push_back(std::move(v));
                SaltarEspacios();
                if (Cur() == ',') {
                    Avanzar();
                    continue;
                }
                if (Cur() == ']') {
                    Avanzar();
                    Profundidad--;
                    return true;
                }
                return Fallo("esperaba ',' o ']' en el array");
            }
        }
        if (c == '"') {
            out = JsonValue();
            out.K = JsonValue::Kind::Text;
            return LeerCadena(out.S);
        }
        if (c == 't') {
            if (S.compare(P, 4, "true") != 0) return Fallo("valor mal escrito");
            P += 4;
            out = JsonValue::MakeBool(true);
            return true;
        }
        if (c == 'f') {
            if (S.compare(P, 5, "false") != 0) return Fallo("valor mal escrito");
            P += 5;
            out = JsonValue::MakeBool(false);
            return true;
        }
        if (c == 'n') {
            if (S.compare(P, 4, "null") != 0) return Fallo("valor mal escrito");
            P += 4;
            out = JsonValue::MakeNull();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            out = JsonValue();
            out.K = JsonValue::Kind::Number;
            return LeerNumero(out.N);
        }
        return Fallo("carácter inesperado");
    }
};

void VolarTexto(const std::string& s, std::string& out) {
    out.push_back('"');
    for (char c : s) {
        unsigned char u = (unsigned char)c;
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (u < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned int)u);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
}

void Volar(const JsonValue& v, std::string& out) {
    switch (v.K) {
        case JsonValue::Kind::Null: out += "null"; break;
        case JsonValue::Kind::Bool: out += v.B ? "true" : "false"; break;
        case JsonValue::Kind::Number: {
            double n = v.N;
            if (std::isfinite(n) && n == (double)(int64_t)n && (n > -9.0e15 && n < 9.0e15)) {
                out += std::to_string((int64_t)n);
            } else if (std::isfinite(n)) {
                char buf[40];
                std::snprintf(buf, sizeof(buf), "%.17g", n);
                out += buf;
            } else {
                out += "null";
            }
            break;
        }
        case JsonValue::Kind::Text: VolarTexto(v.S, out); break;
        case JsonValue::Kind::Array: {
            out.push_back('[');
            bool first = true;
            for (auto& e : v.A) {
                if (!first) out.push_back(',');
                first = false;
                Volar(e, out);
            }
            out.push_back(']');
            break;
        }
        case JsonValue::Kind::Object: {
            out.push_back('{');
            bool first = true;
            for (auto& kv : v.O) {
                if (!first) out.push_back(',');
                first = false;
                VolarTexto(kv.first, out);
                out.push_back(':');
                Volar(kv.second, out);
            }
            out.push_back('}');
            break;
        }
    }
}
}
bool JsonParse(const std::string& src, JsonValue& out, std::string& err) {
    LectorJson L(src);
    err.clear();
    if (!L.LeerValor(out)) {
        err = L.Err;
        return false;
    }
    L.SaltarEspacios();
    if (!L.Fin()) {
        err = "sobran datos tras el valor JSON en la posición " + std::to_string(L.P);
        return false;
    }
    return true;
}
std::string JsonDump(const JsonValue& v) {
    std::string out;
    Volar(v, out);
    return out;
}
bool JsonValue::ParseFrom(const std::string& src, std::string& err) {
    return JsonParse(src, *this, err);
}
std::string JsonValue::Dump() const {
    return JsonDump(*this);
}
}
