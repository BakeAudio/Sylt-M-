#include "lex.hpp"
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
namespace sylt {
namespace {

bool EsLetraUnicode(uint32_t cp) {
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) return true;
    if (cp >= 0xC0 && cp <= 0xD6) return true;
    if (cp >= 0xD8 && cp <= 0xF6) return true;
    if (cp >= 0xF8 && cp <= 0x24F) return true;
    if (cp >= 0x370 && cp <= 0x3FF) return true;
    if (cp >= 0x400 && cp <= 0x52F) return true;
    return false;
}

std::string VisibilidadCaracter(const std::string& text, size_t pos) {
    unsigned char c = (unsigned char)text[pos];
    int cont = 0;
    if ((c & 0xE0) == 0xC0) cont = 1;
    else if ((c & 0xF0) == 0xE0) cont = 2;
    else if ((c & 0xF8) == 0xF0) cont = 3;
    if (cont > 0 && pos + cont < text.size()) {
        bool valida = true;
        for (int i = 1; i <= cont; i++)
            if (((unsigned char)text[pos + i] & 0xC0) != 0x80) valida = false;
        if (valida) return text.substr(pos, (size_t)cont + 1);
    }
    if (cont == 0 && c < 0x80) return std::string(1, (char)c);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "\\x%02x", (unsigned)c);
    return std::string(buf);
}

struct Lexer {
    const SourceFile& Src;
    Reporter& Rep;
    int BaseLine, BaseCol;
    bool HandleIndent;
    std::vector<Token> Out;
    size_t Pos = 0;
    int Line = 1, Col = 1;
    std::vector<int> Indents{0};
    int Depth = 0;
    bool PendingIndent = false;
    bool LastWasContent = false;
    Lexer(const SourceFile& src, Reporter& rep, int bl, int bc, bool indent)
        : Src(src), Rep(rep), BaseLine(bl), BaseCol(bc), HandleIndent(indent) {
        PendingIndent = indent;
    }
    Span Here(int len = 1) const { return Span(BaseLine + Line - 1, BaseCol + Col - 1, len); }
    bool AtEnd() const { return Pos >= Src.Text.size(); }
    char Cur() const { return AtEnd() ? '\0' : Src.Text[Pos]; }
    char Peek(size_t n = 1) const {
        return Pos + n < Src.Text.size() ? Src.Text[Pos + n] : '\0';
    }
    bool Decodifica(size_t p, uint32_t& cp, int& len) const {
        unsigned char c = (unsigned char)Src.Text[p];
        if (c < 0x80) { cp = c; len = 1; return true; }
        int cont = 0;
        if ((c & 0xE0) == 0xC0) { cont = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { cont = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { cont = 3; cp = c & 0x07u; }
        else return false;
        if (p + (size_t)cont >= Src.Text.size()) return false;
        for (int i = 1; i <= cont; i++) {
            unsigned char k = (unsigned char)Src.Text[p + (size_t)i];
            if ((k & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (k & 0x3Fu);
        }
        len = cont + 1;
        return true;
    }
    void ErrorCaracter() {
        std::string vis = VisibilidadCaracter(Src.Text, Pos);
        int salto = 1;
        uint32_t cp = 0;
        int len = 0;
        if (Decodifica(Pos, cp, len)) salto = len;
        Span sp = Here(salto);
        Rep.Emit(Rep.Err("E0101", sp,
            "no sé qué hacer con el carácter `" + vis + "`"));
        for (int i = 0; i < salto; i++) Advance();
    }
    void Advance() {
        if (AtEnd()) return;
        if (Src.Text[Pos] == '\n') { Line++; Col = 1; } else { Col++; }
        Pos++;
    }
    void Push(TT t, std::string text, Span sp, int64_t iv = 0, double fv = 0) {
        Token tk;
        tk.T = t;
        tk.Text = std::move(text);
        tk.Sp = sp;
        tk.IntVal = iv;
        tk.FloatVal = fv;
        Out.push_back(std::move(tk));
    }
    void EmitIndentDedents(int width, int line, int col) {
        int top = Indents.back();
        if (width > top) {
            Indents.push_back(width);
            Push(TT::Indent, "", Span(line, col, 0));
        } else if (width < top) {
            while (Indents.size() > 1 && width < Indents.back()) {
                Indents.pop_back();
                Push(TT::Dedent, "", Span(line, col, 0));
            }
            if (width != Indents.back()) {
                Diag d = Rep.Err("E0103", Span(line, col, 1),
                    "la sangría no coincide con ningún bloque abierto");
                d.Helps.push_back(
                    "cada nivel de sangría debe coincidir con el de la línea que abre el bloque");
                Rep.Emit(d);
            }
        }
    }
    bool SkipEmptyOrCommentLine() {
        while (!AtEnd() && (Cur() == ' ' || Cur() == '\t' || Cur() == '\r')) Advance();
        if (AtEnd()) return true;
        if (Cur() == '\n') { Advance(); return true; }
        if (Cur() == '#') {
            while (!AtEnd() && Cur() != '\n') Advance();
            if (!AtEnd()) Advance();
            return true;
        }
        return false;
    }
    bool EndsInContinuationOp() const {
        if (Out.empty()) return false;
        switch (Out.back().T) {
            case TT::Plus: case TT::Minus: case TT::Star: case TT::Slash:
            case TT::Percent: case TT::Comma: case TT::EqEq: case TT::NotEq:
            case TT::Lt: case TT::LtEq:
            case TT::DotDot: case TT::Amp:
                return true;
            default:
                return false;
        }
    }
    void SkipToContinuationLine() {
        while (true) {
            while (!AtEnd() && (Cur() == ' ' || Cur() == '\t' || Cur() == '\r')) Advance();
            if (AtEnd()) return;
            if (Cur() == '#') {
                while (!AtEnd() && Cur() != '\n') Advance();
                if (!AtEnd()) Advance();
                continue;
            }
            if (Cur() == '\n') { Advance(); continue; }
            return;
        }
    }
    void LexNumber() {
        Span sp = Here();
        std::string raw;
        bool isFloat = false;
        while (isdigit((unsigned char)Cur()) || Cur() == '_') {
            if (Cur() != '_') raw.push_back(Cur());
            Advance();
        }
        if (Cur() == '.' && isdigit((unsigned char)Peek())) {
            isFloat = true;
            raw.push_back('.');
            Advance();
            while (isdigit((unsigned char)Cur()) || Cur() == '_') {
                if (Cur() != '_') raw.push_back(Cur());
                Advance();
            }
        }
        if (Cur() == 'e' || Cur() == 'E') {
            char nx = Peek(), nx2 = Peek(2);
            if (isdigit((unsigned char)nx) ||
                ((nx == '+' || nx == '-') && isdigit((unsigned char)nx2))) {
                isFloat = true;
                raw.push_back('e');
                Advance();
                if (Cur() == '+' || Cur() == '-') { raw.push_back(Cur()); Advance(); }
                while (isdigit((unsigned char)Cur())) { raw.push_back(Cur()); Advance(); }
            }
        }
        sp.Len = (int)raw.size();
        if (isFloat) {
            Push(TT::Float, raw, sp, 0, strtod(raw.c_str(), nullptr));
        } else {
            errno = 0;
            int64_t v = strtoll(raw.c_str(), nullptr, 10);
            if (errno == ERANGE) {
                Rep.Emit(Rep.Err("E0102", sp,
                    "este número es demasiado grande para `int` (máximo 9.223.372.036.854.775.807)"));
                v = 0;
            }
            Push(TT::Int, raw, sp, v);
        }
    }
    void LexString() {
        Span sp = Here();
        Advance();
        std::string raw;
        bool closed = false;
        while (!AtEnd()) {
            char c = Cur();
            if (c == '"') { Advance(); closed = true; break; }
            if (c == '\n') break;
            if (c == '\\') {
                char nx = Peek();
                if (nx == 'n') { raw.push_back('\n'); Advance(); Advance(); continue; }
                if (nx == 't') { raw.push_back('\t'); Advance(); Advance(); continue; }
                if (nx == 'r') { raw.push_back('\r'); Advance(); Advance(); continue; }
                if (nx == '"') { raw.push_back('"'); Advance(); Advance(); continue; }
                if (nx == '\\') { raw.push_back('\\'); Advance(); Advance(); continue; }
                Span es = Here(2);
                Diag d = Rep.Err("E0102", es,
                    std::string("escape desconocido `\\") + nx + "` en un string");
                d.Helps.push_back("los escapes válidos son \\n (salto), \\t (tabulador), "
                                  "\\r (retorno), \\\" y \\\\");
                d.Helps.push_back("para una llave literal usa dobles: {{ o }}");
                Rep.Emit(d);
                Advance(); Advance();
                continue;
            }
            raw.push_back(c);
            Advance();
        }
        if (!closed) {
            Diag d = Rep.Err("E0102", sp, "no encuentro la comilla de cierre de este string");
            d.Helps.push_back("los strings van entre comillas dobles y no pueden saltar de línea");
            Rep.Emit(d);
        }
        sp.Len = (int)raw.size() + 2;
        Push(TT::Str, raw, sp);
    }
    void LexIdent() {
        Span sp = Here();
        std::string raw;
        while (!AtEnd()) {
            char c = Cur();
            if (isalpha((unsigned char)c) || isdigit((unsigned char)c) || c == '_') {
                raw.push_back(c);
                Advance();
                continue;
            }
            if ((unsigned char)c < 0x80) break;
            uint32_t cp = 0;
            int len = 0;
            if (!Decodifica(Pos, cp, len) || !EsLetraUnicode(cp)) break;
            raw.append(Src.Text, Pos, (size_t)len);
            for (int i = 0; i < len; i++) Advance();
        }
        sp.Len = (int)raw.size();
        TT t = TT::Ident;
        if (raw == "fn") t = TT::KwFn;
        else if (raw == "let") t = TT::KwLet;
        else if (raw == "var") t = TT::KwVar;
        else if (raw == "type") t = TT::KwType;
        else if (raw == "if") t = TT::KwIf;
        else if (raw == "elif") t = TT::KwElif;
        else if (raw == "else") t = TT::KwElse;
        else if (raw == "while") t = TT::KwWhile;
        else if (raw == "for") t = TT::KwFor;
        else if (raw == "in") t = TT::KwIn;
        else if (raw == "return") t = TT::KwReturn;
        else if (raw == "break") t = TT::KwBreak;
        else if (raw == "continue") t = TT::KwContinue;
        else if (raw == "pass") t = TT::KwPass;
        else if (raw == "and") t = TT::KwAnd;
        else if (raw == "or") t = TT::KwOr;
        else if (raw == "not") t = TT::KwNot;
        else if (raw == "true") t = TT::KwTrue;
        else if (raw == "false") t = TT::KwFalse;
        else if (raw == "self") t = TT::KwSelf;
        else if (raw == "mut") t = TT::KwMut;
        else if (raw == "match") t = TT::KwMatch;
        else if (raw == "case") t = TT::KwCase;
        else if (raw == "import") t = TT::KwImport;
        else if (raw == "foreign") t = TT::KwForeign;
        else if (raw == "enum") t = TT::KwEnum;
        Push(t, raw, sp);
    }
    void LexOperator() {
        char c = Cur();
        Span sp = Here(2);
        auto two = [&](const char* s) { return Cur() == s[0] && Peek() == s[1]; };
        if (two("->")) { Advance(); Advance(); Push(TT::Arrow, "->", sp); return; }
        if (two("..")) { Advance(); Advance(); Push(TT::DotDot, "..", sp); return; }
        if (two("==")) { Advance(); Advance(); Push(TT::EqEq, "==", sp); return; }
        if (two("!=")) { Advance(); Advance(); Push(TT::NotEq, "!=", sp); return; }
        if (two("<=")) { Advance(); Advance(); Push(TT::LtEq, "<=", sp); return; }
        if (two(">=")) { Advance(); Advance(); Push(TT::GtEq, ">=", sp); return; }
        if (two("+=")) { Advance(); Advance(); Push(TT::PlusEq, "+=", sp); return; }
        if (two("-=")) { Advance(); Advance(); Push(TT::MinusEq, "-=", sp); return; }
        if (two("*=")) { Advance(); Advance(); Push(TT::StarEq, "*=", sp); return; }
        if (two("/=")) { Advance(); Advance(); Push(TT::SlashEq, "/=", sp); return; }
        sp.Len = 1;
        switch (c) {
            case '+': Push(TT::Plus, "+", sp); break;
            case '-': Push(TT::Minus, "-", sp); break;
            case '*': Push(TT::Star, "*", sp); break;
            case '/': Push(TT::Slash, "/", sp); break;
            case '%': Push(TT::Percent, "%", sp); break;
            case '=': Push(TT::Eq, "=", sp); break;
            case '<': Push(TT::Lt, "<", sp); break;
            case '>': Push(TT::Gt, ">", sp); break;
            case '.': Push(TT::Dot, ".", sp); break;
            case ',': Push(TT::Comma, ",", sp); break;
            case ':': Push(TT::Colon, ":", sp); break;
            case '(': Push(TT::LParen, "(", sp); Depth++; break;
            case ')': Push(TT::RParen, ")", sp); if (Depth > 0) Depth--; break;
            case '[': Push(TT::LBracket, "[", sp); Depth++; break;
            case ']': Push(TT::RBracket, "]", sp); if (Depth > 0) Depth--; break;
            case '{': Push(TT::LBrace, "{", sp); Depth++; break;
            case '}': Push(TT::RBrace, "}", sp); if (Depth > 0) Depth--; break;
            case '&': Push(TT::Amp, "&", sp); break;
            default:
                Rep.Emit(Rep.Err("E0101", sp,
                    std::string("no sé qué hacer con el carácter `") + c + "`"));
                break;
        }
        Advance();
    }
    void Run() {
        while (true) {
            if (HandleIndent && PendingIndent && Depth == 0) {
                if (AtEnd()) break;
                int width = 0;
                bool sawSpace = false, sawTab = false;
                while (!AtEnd() && (Cur() == ' ' || Cur() == '\t')) {
                    if (Cur() == ' ') { width++; sawSpace = true; }
                    else { width = (width / 8 + 1) * 8; sawTab = true; }
                    Advance();
                }
                if (AtEnd()) break;
                if (Cur() == '\n') { Advance(); continue; }
                if (Cur() == '#') {
                    while (!AtEnd() && Cur() != '\n') Advance();
                    if (!AtEnd()) Advance();
                    continue;
                }
                if (sawSpace && sawTab) {
                    Rep.Emit(Rep.Err("E0104", Here(),
                        "esta línea mezcla tabuladores y espacios en la sangría"));
                }
                PendingIndent = false;
                EmitIndentDedents(width, Line, Col);
            }
            if (AtEnd()) break;
            char c = Cur();
            if (c == ' ' || c == '\t' || c == '\r') { Advance(); continue; }
            if (c == '#') { while (!AtEnd() && Cur() != '\n') Advance(); continue; }
            if (c == '\n') {
                Advance();
                if (Depth > 0) continue;
                if (EndsInContinuationOp()) {
                    SkipToContinuationLine();
                    LastWasContent = true;
                    continue;
                }
                Push(TT::Newline, "\\n", Span(BaseLine + Line - 2, 1, 1));
                LastWasContent = false;
                if (HandleIndent) PendingIndent = true;
                continue;
            }
            if (isdigit((unsigned char)c)) { LexNumber(); LastWasContent = true; continue; }
            if (isalpha((unsigned char)c) || c == '_') { LexIdent(); LastWasContent = true; continue; }
            if ((unsigned char)c >= 0x80) {
                uint32_t cp = 0;
                int len = 0;
                if (Decodifica(Pos, cp, len) && EsLetraUnicode(cp)) {
                    LexIdent();
                    LastWasContent = true;
                    continue;
                }
                ErrorCaracter();
                LastWasContent = true;
                continue;
            }
            if (c == '"') { LexString(); LastWasContent = true; continue; }
            if (c == '!' && Peek() != '=') {
                Diag d = Rep.Err("E0101", Here(1), "no existe el operador `!`");
                d.Helps.push_back("la negación en Sylt es `not`: `not Visible`");
                Rep.Emit(d);
                Advance();
                continue;
            }
            if (c == '&' && Peek() == '&') {
                Diag d = Rep.Err("E0101", Here(2), "no existe el operador `&&`");
                d.Helps.push_back("en Sylt se escribe `and`: `if A and B`");
                Rep.Emit(d);
                Advance(); Advance();
                continue;
            }
            if (c == '|') {
                Diag d = Rep.Err("E0101", Here(1), "no existe el operador `|`");
                d.Helps.push_back("en Sylt se escribe `or`: `if A or B`");
                Rep.Emit(d);
                Advance();
                continue;
            }
            if (c == ';') {
                Diag d = Rep.Err("E0101", Here(1), "Sylt no usa punto y coma");
                d.Helps.push_back("cada sentencia va en su propia línea, como en Python");
                Rep.Emit(d);
                Advance();
                continue;
            }
            if (c == '+' && !Out.empty() && Out.back().T == TT::Plus) {
                Diag d = Rep.Err("E0101", Here(2), "no existe el operador `++`");
                d.Helps.push_back("escribe `X += 1` para incrementar");
                Rep.Emit(d);
                Advance(); Advance();
                continue;
            }
            LexOperator();
            LastWasContent = true;
        }
        if (HandleIndent) {
            if (LastWasContent && Depth == 0)
                Push(TT::Newline, "\\n", Span(BaseLine + Line - 1, 1, 1));
            while (Indents.size() > 1) {
                Indents.pop_back();
                Push(TT::Dedent, "", Span(BaseLine + Line - 1, 1, 0));
            }
        }
        Push(TT::Eof, "", Span(BaseLine + Line - 1, BaseCol + Col, 0));
    }
};
}
std::vector<Token> Lex(const SourceFile& src, Reporter& rep) {
    Lexer lx(src, rep, 1, 1, true);
    lx.Run();
    return std::move(lx.Out);
}
std::vector<Token> LexSubstring(const std::string& text, int line, int col, Reporter& rep) {
    SourceFile tmp;
    tmp.Load("<interpolación>", text);
    Lexer lx(tmp, rep, line, col, false);
    lx.Run();
    return std::move(lx.Out);
}
const char* TokName(TT t) {
    switch (t) {
        case TT::Eof: return "fin de archivo";
        case TT::Newline: return "salto de línea";
        case TT::Indent: return "sangría";
        case TT::Dedent: return "fin de bloque";
        case TT::Ident: return "un nombre";
        case TT::Int: case TT::Float: return "un número";
        case TT::Str: return "un string";
        case TT::KwFn: return "`fn`";
        case TT::KwLet: return "`let`";
        case TT::KwVar: return "`var`";
        case TT::KwType: return "`type`";
        case TT::KwIf: return "`if`";
        case TT::KwElif: return "`elif`";
        case TT::KwElse: return "`else`";
        case TT::KwWhile: return "`while`";
        case TT::KwFor: return "`for`";
        case TT::KwIn: return "`in`";
        case TT::KwReturn: return "`return`";
        case TT::KwBreak: return "`break`";
        case TT::KwContinue: return "`continue`";
        case TT::KwPass: return "`pass`";
        case TT::KwAnd: return "`and`";
        case TT::KwOr: return "`or`";
        case TT::KwNot: return "`not`";
        case TT::KwTrue: case TT::KwFalse: return "un bool";
        case TT::KwSelf: return "`self`";
        case TT::KwMut: return "`mut`";
        case TT::KwMatch: return "`match`";
        case TT::KwCase: return "`case`";
        case TT::KwImport: return "`import`";
        case TT::KwForeign: return "`foreign`";
        case TT::KwEnum: return "`enum`";
        case TT::Amp: return "`&`";
        default: return "un símbolo";
    }
}
}
