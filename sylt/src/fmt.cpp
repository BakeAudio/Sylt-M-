#include "fmt.hpp"
#include "lex.hpp"
#include "loader.hpp"
#include "parse.hpp"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
namespace sylt {
namespace {

struct RawTok {
    TT T;
    std::string Text;
};

TT ClasificarPalabra(const std::string& w) {
    if (w == "fn") return TT::KwFn;
    if (w == "let") return TT::KwLet;
    if (w == "var") return TT::KwVar;
    if (w == "type") return TT::KwType;
    if (w == "if") return TT::KwIf;
    if (w == "elif") return TT::KwElif;
    if (w == "else") return TT::KwElse;
    if (w == "while") return TT::KwWhile;
    if (w == "for") return TT::KwFor;
    if (w == "in") return TT::KwIn;
    if (w == "return") return TT::KwReturn;
    if (w == "break") return TT::KwBreak;
    if (w == "continue") return TT::KwContinue;
    if (w == "pass") return TT::KwPass;
    if (w == "and") return TT::KwAnd;
    if (w == "or") return TT::KwOr;
    if (w == "not") return TT::KwNot;
    if (w == "true") return TT::KwTrue;
    if (w == "false") return TT::KwFalse;
    if (w == "self") return TT::KwSelf;
    if (w == "mut") return TT::KwMut;
    if (w == "match") return TT::KwMatch;
    if (w == "case") return TT::KwCase;
    if (w == "import") return TT::KwImport;
    if (w == "foreign") return TT::KwForeign;
    if (w == "enum") return TT::KwEnum;
    return TT::Ident;
}

bool EsOperando(TT t) {
    switch (t) {
        case TT::Ident: case TT::Int: case TT::Float: case TT::Str:
        case TT::KwTrue: case TT::KwFalse: case TT::KwSelf:
        case TT::RParen: case TT::RBracket: case TT::RBrace:
            return true;
        default:
            return false;
    }
}

size_t BuscarComentario(const std::string& s) {
    bool enString = false;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (enString) {
            if (c == '\\') {
                i += 2;
                continue;
            }
            if (c == '"') enString = false;
            i++;
            continue;
        }
        if (c == '"') {
            enString = true;
            i++;
            continue;
        }
        if (c == '#') return i;
        i++;
    }
    return std::string::npos;
}

std::vector<RawTok> LexLinea(const std::string& s) {
    std::vector<RawTok> out;
    size_t i = 0, n = s.size();
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            i++;
            continue;
        }
        if (c == '"') {
            size_t j = i + 1;
            while (j < n && s[j] != '"') {
                if (s[j] == '\\' && j + 1 < n) j += 2;
                else j++;
            }
            if (j < n) j++;
            out.push_back({TT::Str, s.substr(i, j - i)});
            i = j;
            continue;
        }
        if (isdigit((unsigned char)c)) {
            size_t j = i;
            bool esFloat = false;
            while (j < n && (isdigit((unsigned char)s[j]) || s[j] == '_')) j++;
            if (j < n && s[j] == '.' && j + 1 < n && isdigit((unsigned char)s[j + 1])) {
                esFloat = true;
                j++;
                while (j < n && (isdigit((unsigned char)s[j]) || s[j] == '_')) j++;
            }
            if (j < n && (s[j] == 'e' || s[j] == 'E')) {
                size_t k = j + 1;
                if (k < n && (s[k] == '+' || s[k] == '-')) k++;
                if (k < n && isdigit((unsigned char)s[k])) {
                    while (k < n && isdigit((unsigned char)s[k])) k++;
                    j = k;
                    esFloat = true;
                }
            }
            out.push_back({esFloat ? TT::Float : TT::Int, s.substr(i, j - i)});
            i = j;
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_' || (unsigned char)c >= 0x80) {
            size_t j = i;
            while (j < n && (isalnum((unsigned char)s[j]) || s[j] == '_' ||
                             (unsigned char)s[j] >= 0x80)) j++;
            std::string w = s.substr(i, j - i);
            out.push_back({ClasificarPalabra(w), w});
            i = j;
            continue;
        }
        char c2 = i + 1 < n ? s[i + 1] : '\0';
        if (c == '-' && c2 == '>') { out.push_back({TT::Arrow, "->"}); i += 2; continue; }
        if (c == '.' && c2 == '.') { out.push_back({TT::DotDot, ".."}); i += 2; continue; }
        if (c == '=' && c2 == '=') { out.push_back({TT::EqEq, "=="}); i += 2; continue; }
        if (c == '!' && c2 == '=') { out.push_back({TT::NotEq, "!="}); i += 2; continue; }
        if (c == '<' && c2 == '=') { out.push_back({TT::LtEq, "<="}); i += 2; continue; }
        if (c == '>' && c2 == '=') { out.push_back({TT::GtEq, ">="}); i += 2; continue; }
        if (c == '+' && c2 == '=') { out.push_back({TT::PlusEq, "+="}); i += 2; continue; }
        if (c == '-' && c2 == '=') { out.push_back({TT::MinusEq, "-="}); i += 2; continue; }
        if (c == '*' && c2 == '=') { out.push_back({TT::StarEq, "*="}); i += 2; continue; }
        if (c == '/' && c2 == '=') { out.push_back({TT::SlashEq, "/="}); i += 2; continue; }
        switch (c) {
            case '+': out.push_back({TT::Plus, "+"}); break;
            case '-': out.push_back({TT::Minus, "-"}); break;
            case '*': out.push_back({TT::Star, "*"}); break;
            case '/': out.push_back({TT::Slash, "/"}); break;
            case '%': out.push_back({TT::Percent, "%"}); break;
            case '=': out.push_back({TT::Eq, "="}); break;
            case '<': out.push_back({TT::Lt, "<"}); break;
            case '>': out.push_back({TT::Gt, ">"}); break;
            case '.': out.push_back({TT::Dot, "."}); break;
            case ',': out.push_back({TT::Comma, ","}); break;
            case ':': out.push_back({TT::Colon, ":"}); break;
            case '(': out.push_back({TT::LParen, "("}); break;
            case ')': out.push_back({TT::RParen, ")"}); break;
            case '[': out.push_back({TT::LBracket, "["}); break;
            case ']': out.push_back({TT::RBracket, "]"}); break;
            case '{': out.push_back({TT::LBrace, "{"}); break;
            case '}': out.push_back({TT::RBrace, "}"}); break;
            case '&': out.push_back({TT::Amp, "&"}); break;
            default: out.push_back({TT::Ident, std::string(1, c)}); break;
        }
        i++;
    }
    return out;
}

std::string ReEspaciar(const std::vector<RawTok>& toks) {
    std::string out;
    bool primero = true;
    bool ultimoFueOperando = false;
    bool prevFueMenosUnario = false;
    TT prevT = TT::Eof;
    for (const RawTok& t : toks) {
        bool espacio;
        if (primero) espacio = false;
        else if (t.T == TT::Comma || t.T == TT::Colon || t.T == TT::Dot ||
                 t.T == TT::RParen || t.T == TT::RBracket || t.T == TT::DotDot)
            espacio = false;
        else if (prevT == TT::LParen || prevT == TT::LBracket) espacio = false;
        else if (prevT == TT::Dot || prevT == TT::DotDot) espacio = false;
        else if (prevT == TT::Amp || prevFueMenosUnario) espacio = false;
        else if (t.T == TT::RBrace) espacio = (prevT != TT::LBrace);
        else if (t.T == TT::LParen || t.T == TT::LBracket) espacio = !ultimoFueOperando;
        else if (t.T == TT::LBrace) espacio = true;
        else espacio = true;
        if (espacio) out.push_back(' ');
        out += t.Text;
        primero = false;
        bool operandoAhora = EsOperando(t.T);
        if (t.T == TT::Minus) {
            prevFueMenosUnario = !ultimoFueOperando;
            operandoAhora = prevFueMenosUnario;
        } else if (t.T == TT::Amp) {
            prevFueMenosUnario = false;
            operandoAhora = true;
        } else {
            prevFueMenosUnario = false;
        }
        ultimoFueOperando = operandoAhora;
        prevT = t.T;
    }
    return out;
}

struct LineaLogica {
    int Primera = 0;
    int Ultima = 0;
    int Profundidad = 0;
};

std::vector<LineaLogica> AgruparLineas(const std::vector<Token>& toks) {
    std::vector<LineaLogica> out;
    int profundidad = 0;
    bool abierta = false;
    for (const Token& t : toks) {
        if (t.T == TT::Indent) {
            profundidad++;
            continue;
        }
        if (t.T == TT::Dedent) {
            profundidad--;
            continue;
        }
        if (t.T == TT::Newline || t.T == TT::Eof) {
            abierta = false;
            continue;
        }
        if (!abierta) {
            LineaLogica L;
            L.Primera = t.Sp.Line;
            L.Ultima = t.Sp.Line;
            L.Profundidad = profundidad;
            out.push_back(L);
            abierta = true;
        } else if (t.Sp.Line > out.back().Ultima) {
            out.back().Ultima = t.Sp.Line;
        }
    }
    return out;
}

int SangriaOriginal(const std::string& linea) {
    int col = 0;
    for (char c : linea) {
        if (c == ' ') col++;
        else if (c == '\t') col = (col / 8 + 1) * 8;
        else break;
    }
    return col;
}

std::vector<std::string> PartirLineas(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty() && cur.back() == '\r') cur.pop_back();
    out.push_back(cur);
    return out;
}

bool FormatearDeTokens(const std::string& text, const std::vector<Token>& toks,
                       std::string& out) {
    std::vector<LineaLogica> logicas = AgruparLineas(toks);
    std::vector<std::string> lineas = PartirLineas(text);
    int N = (int)lineas.size();
    std::vector<int> Reclamo(N + 2, -1);
    for (size_t i = 0; i < logicas.size(); i++) {
        for (int l = logicas[i].Primera; l <= logicas[i].Ultima && l <= N; l++)
            Reclamo[l] = (int)i;
    }
    std::vector<int> SigLogica(N + 2, -1);
    int prox = -1;
    for (int l = N; l >= 1; l--) {
        SigLogica[l] = prox;
        if (Reclamo[l] >= 0 && logicas[Reclamo[l]].Primera == l) prox = Reclamo[l];
    }
    std::vector<std::string> salida;
    int prevProf = -1;
    int pendBlancas = 0;
    bool emitidoAlgo = false;
    auto flushBlancas = [&]() {
        if (emitidoAlgo && pendBlancas > 0) salida.push_back("");
        pendBlancas = 0;
    };
    for (int l = 1; l <= N; l++) {
        const std::string& original = lineas[l - 1];
        if (Reclamo[l] >= 0) {
            const LineaLogica& L = logicas[Reclamo[l]];
            bool esPrimera = (l == L.Primera);
            int prof = esPrimera ? L.Profundidad : L.Profundidad + 1;
            std::string codigo = original;
            std::string comentario;
            size_t pos = BuscarComentario(original);
            if (pos != std::string::npos) {
                codigo = original.substr(0, pos);
                comentario = Trim(original.substr(pos));
            }
            std::string contenido = Trim(codigo);
            flushBlancas();
            if (contenido.empty() && comentario.empty()) continue;
            std::string linea;
            auto crudos = LexLinea(contenido);
            if (!crudos.empty() && crudos[0].Text == "include") {
                linea = std::string((size_t)(prof * 4), ' ') + contenido;
            } else if (!contenido.empty()) {
                linea = std::string((size_t)(prof * 4), ' ') + ReEspaciar(crudos);
            } else {
                linea = std::string((size_t)(prof * 4), ' ') + comentario;
            }
            if (!contenido.empty() && !comentario.empty()) linea += " " + comentario;
            salida.push_back(linea);
            emitidoAlgo = true;
            prevProf = prof;
        } else {
            std::string contenido = Trim(original);
            if (contenido.empty()) {
                if (emitidoAlgo) pendBlancas++;
                continue;
            }
            int indentOrig = SangriaOriginal(original);
            int prof;
            int sig = SigLogica[l];
            if (prevProf >= 0) {
                int prevLinea = 1;
                for (int k = l - 1; k >= 1; k--) {
                    if (Reclamo[k] >= 0) {
                        prevLinea = logicas[Reclamo[k]].Primera;
                        break;
                    }
                }
                int indentPrev = SangriaOriginal(lineas[prevLinea - 1]);
                if (indentOrig >= indentPrev) {
                    prof = prevProf;
                } else if (sig >= 0 &&
                           indentOrig >= SangriaOriginal(lineas[logicas[sig].Primera - 1])) {
                    prof = logicas[sig].Profundidad;
                } else {
                    prof = prevProf;
                }
            } else if (sig >= 0) {
                prof = logicas[sig].Profundidad;
            } else {
                prof = 0;
            }
            flushBlancas();
            salida.push_back(std::string((size_t)(prof * 4), ' ') + contenido);
            emitidoAlgo = true;
            prevProf = prof;
        }
    }
    std::string res;
    for (size_t i = 0; i < salida.size(); i++) {
        res += salida[i];
        res.push_back('\n');
    }
    out = res;
    return true;
}

void ArchivosDeDir(const std::string& ruta, std::vector<std::string>& out, int depth) {
    if (depth > 16) return;
    DIR* d = opendir(ruta.c_str());
    if (!d) return;
    struct dirent* e;
    std::vector<std::string> nombres;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || n.empty() || n[0] == '.') continue;
        nombres.push_back(n);
    }
    closedir(d);
    std::sort(nombres.begin(), nombres.end());
    for (auto& n : nombres) {
        std::string completo = ruta + "/" + n;
        struct stat b;
        if (stat(completo.c_str(), &b) != 0) continue;
        if (S_ISDIR(b.st_mode)) ArchivosDeDir(completo, out, depth + 1);
        else if (S_ISREG(b.st_mode) && n.size() > 3 &&
                 n.compare(n.size() - 3, 3, ".sy") == 0)
            out.push_back(completo);
    }
}
}
bool FormatSource(const std::string& name, const std::string& text, std::string& out,
                  std::vector<Diag>* sink) {
    SourceFile src;
    src.Load(name, text);
    Reporter rep(src, sink == nullptr);
    std::vector<Diag> buf;
    if (sink) rep.SetSink(&buf);
    auto toks = Lex(src, rep);
    if (rep.ErrorCount() == 0) Parse(toks, src, rep);
    if (rep.ErrorCount() > 0) {
        if (sink) *sink = std::move(buf);
        return false;
    }
    FormatearDeTokens(text, toks, out);
    return true;
}
int RunFmt(std::vector<std::string> Rutas, bool SoloComprobar) {
    if (Rutas.empty()) {
        std::cerr << "falta el archivo .sy a formatear\n";
        std::cerr << "prueba: syltc fmt archivo.sy   (o `syltc fmt .` para un directorio)\n";
        return 2;
    }
    std::vector<std::string> archivos;
    for (auto& r : Rutas) {
        struct stat b;
        if (stat(r.c_str(), &b) != 0) {
            std::cerr << "no puedo leer `" << r << "`: ¿existe y tiene permisos?\n";
            return 2;
        }
        if (S_ISDIR(b.st_mode)) ArchivosDeDir(r, archivos, 0);
        else archivos.push_back(r);
    }
    if (archivos.empty()) {
        std::cout << "no hay archivos .sy que formatear\n";
        return 0;
    }
    std::sort(archivos.begin(), archivos.end());
    int fallos = 0;
    int distintos = 0;
    for (auto& ruta : archivos) {
        std::string text;
        if (!ReadFile(ruta, text)) {
            std::cerr << "no puedo leer `" << ruta << "`: ¿existe y tiene permisos?\n";
            fallos++;
            continue;
        }
        std::string out;
        if (!FormatSource(ruta, text, out, nullptr)) {
            std::cerr << "no se pudo formatear: `" << ruta
                      << "` tiene errores de sintaxis\n";
            fallos++;
            continue;
        }
        if (out == text) continue;
        distintos++;
        if (SoloComprobar) {
            std::cout << "sin formatear: " << ruta << "\n";
            continue;
        }
        std::ofstream F(ruta, std::ios::binary);
        if (!F) {
            std::cerr << "no puedo escribir `" << ruta << "`\n";
            fallos++;
            continue;
        }
        F << out;
        std::cout << "formateado: " << ruta << "\n";
    }
    if (SoloComprobar && distintos > 0) return 1;
    return fallos > 0 ? 1 : 0;
}
}
