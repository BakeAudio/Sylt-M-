#include "parse.hpp"
#include <memory>
namespace sylt {
namespace {
struct Parser {
    const std::vector<Token>& T;
    const SourceFile& Src;
    Reporter& Rep;
    size_t I = 0;
    bool Failed = false;
    Parser(const std::vector<Token>& toks, const SourceFile& src, Reporter& rep)
        : T(toks), Src(src), Rep(rep) {}
    const Token& Cur() const { return T[I]; }
    const Token& Peek(size_t n = 1) const {
        return I + n < T.size() ? T[I + n] : T.back();
    }
    bool Is(TT t) const { return Cur().T == t; }
    bool IsEof() const { return Cur().T == TT::Eof; }
    void Advance() { if (!IsEof()) I++; }
    bool Accept(TT t) { if (Is(t)) { Advance(); return true; } return false; }
    void Fail(const std::string& expected) {
        if (Failed) return;
        Failed = true;
        std::string msg = "se esperaba " + expected;
        if (Cur().T == TT::Eof) msg += ", pero el archivo termina aquí";
        else msg += std::string(", pero encontré ") + TokName(Cur().T);
        Diag d = Rep.Err("E0201", Cur().Sp, msg);
        std::string hint = Hint();
        if (!hint.empty()) d.Helps.push_back(hint);
        Rep.Emit(d);
    }
    std::string Hint() {
        switch (Cur().T) {
            case TT::Eof:
                return "quizá falta cerrar un bloque; repasa la sangría de las últimas líneas";
            case TT::Ident:
                return "repasa que la línea anterior esté completa (¿falta `:` o un paréntesis?)";
            case TT::KwFn:
                return "las sentencias no pueden empezar con `fn`; los métodos van dentro de `type`";
            case TT::Newline:
                return "la sentencia anterior parece incompleta";
            case TT::KwLet: case TT::KwVar:
                return "una sentencia por línea: `let X = 1` y en la línea siguiente la siguiente";
            default:
                return "";
        }
    }
    const Token& Expect(TT t, const std::string& what) {
        if (Is(t)) { const Token& tk = Cur(); Advance(); return tk; }
        Fail(what);
        return Cur();
    }
    void SyncLine() {
        while (!IsEof() && !Is(TT::Newline) && !Is(TT::Dedent)) Advance();
    }
    TypeAnn ParseTypeAnnBase(bool allowVoid) {
        TypeAnn a;
        a.Sp = Cur().Sp;
        if (Is(TT::Ident)) {
            const std::string& n = Cur().Text;
            if (n == "int" || n == "float" || n == "bool" || n == "string" || n == "void") {
                if (n == "void" && !allowVoid) {
                    Diag d = Rep.Err("E0408", Cur().Sp,
                        "`void` solo puede ser el tipo de retorno de una función");
                    Rep.Emit(d);
                }
                a.k = TypeAnn::K::Prim;
                a.Name = n;
                Advance();
                return a;
            }
            a.k = TypeAnn::K::Ident;
            a.Name = n;
            if (Is(TT::Dot) && Peek().T == TT::Ident) {
                Advance();
                a.Name += "." + Cur().Text;
            }
            Advance();
            return a;
        }
        if (Accept(TT::LBracket)) {
            a.k = TypeAnn::K::Array;
            a.Sub.push_back(ParseTypeAnnBase(false));
            Expect(TT::RBracket, "`]` que cierra el tipo array");
            return a;
        }
        if (Accept(TT::LBrace)) {
            a.k = TypeAnn::K::Map;
            a.Sub.push_back(ParseTypeAnnBase(false));
            Expect(TT::Colon, "`:` entre clave y valor del tipo mapa");
            a.Sub.push_back(ParseTypeAnnBase(false));
            Expect(TT::RBrace, "`}` que cierra el tipo mapa");
            return a;
        }
        if (Accept(TT::LParen)) {
            a.k = TypeAnn::K::Tuple;
            a.Sub.push_back(ParseTypeAnnBase(false));
            Expect(TT::Comma, "`,`: las tuplas tienen al menos dos elementos");
            a.Sub.push_back(ParseTypeAnnBase(false));
            while (Accept(TT::Comma)) a.Sub.push_back(ParseTypeAnnBase(false));
            Expect(TT::RParen, "`)` que cierra el tipo tupla");
            return a;
        }
        Fail("un tipo (int, string, [int], {string: int}, MiStruct...)");
        return a;
    }
    TypeAnn ParseTypeAnn(bool allowVoid) {
        if (Is(TT::Amp)) {
            TypeAnn a;
            a.Sp = Cur().Sp;
            a.k = TypeAnn::K::Borrow;
            Advance();
            if (Accept(TT::KwMut)) a.Mut = true;
            a.Sub.push_back(ParseTypeAnnBase(false));
            return a;
        }
        return ParseTypeAnnBase(allowVoid);
    }
    ProgramP ParseProgram() {
        ProgramP p(new Program);
        while (!IsEof()) {
            if (Accept(TT::Newline) || Accept(TT::Dedent) || Accept(TT::Indent)) continue;
            if (Is(TT::KwFn)) {
                p->Fns.push_back(ParseFn(false, ""));
            } else if (Is(TT::KwType)) {
                p->Types.push_back(ParseTypeDecl());
            } else if (Is(TT::KwEnum)) {
                p->Enums.push_back(ParseEnum());
            } else if (Is(TT::KwImport)) {
                ImportDecl im;
                im.Sp = Cur().Sp;
                Advance();
                const Token& n = Expect(TT::Ident, "el nombre del módulo a importar");
                im.Module = n.Text;
                if (Accept(TT::Dot)) {
                    const Token& s = Expect(TT::Ident, "el submódulo tras el punto");
                    im.Module += "." + s.Text;
                }
                p->Imports.push_back(im);
                if (!Is(TT::Dedent) && !IsEof())
                    Expect(TT::Newline, "un salto de línea tras el import");
            } else if (Is(TT::KwForeign)) {
                ParseForeign(p.get());
            } else {
                Fail("`fn`, `type`, `enum`, `import` o `foreign`");
                SyncLine();
            }
        }
        return p;
    }
    std::unique_ptr<FnDecl> ParseFn(bool isMethod, const std::string& owner, bool foreign = false) {
        auto fn = std::make_unique<FnDecl>();
        Expect(TT::KwFn, "`fn`");
        const Token& nt = Expect(TT::Ident, "el nombre de la función");
        fn->Name = nt.Text;
        fn->Sp = nt.Sp;
        fn->IsMethod = isMethod;
        fn->OwnerStruct = owner;
        fn->Foreign = foreign;
        Expect(TT::LParen, "`(` tras el nombre");
        if (!Is(TT::RParen)) {
            while (true) {
                Param p;
                p.Sp = Cur().Sp;
                if (Accept(TT::KwMut)) {
                    if (Accept(TT::KwSelf)) {
                        p.IsSelf = true;
                        p.Mut = true;
                    } else {
                        const Token& n = Expect(TT::Ident, "un nombre de parámetro");
                        p.Name = n.Text;
                        p.Mut = true;
                        Expect(TT::Colon, "`:` con el tipo del parámetro (N: int)");
                        p.HasTy = true;
                        p.Ty = ParseTypeAnn(false);
                    }
                } else if (Accept(TT::KwSelf)) {
                    p.IsSelf = true;
                } else {
                    const Token& n = Expect(TT::Ident, "un nombre de parámetro");
                    p.Name = n.Text;
                    Expect(TT::Colon,
                        "`:` con el tipo del parámetro, como `N: int` (todos los parámetros "
                        "llevan tipo, solo `self` va sin él)");
                    p.HasTy = true;
                    p.Ty = ParseTypeAnn(false);
                }
                fn->Params.push_back(std::move(p));
                if (!Accept(TT::Comma)) break;
            }
        }
        Expect(TT::RParen, "`)` que cierra los parámetros");
        if (Accept(TT::Arrow)) {
            fn->HasRet = true;
            fn->Ret = ParseTypeAnn(true);
        }
        if (foreign) {
            if (!Is(TT::Dedent) && !IsEof())
                Expect(TT::Newline, "un salto de línea (una función foreign por línea)");
            return fn;
        }
        fn->Body = ParseBlock("el cuerpo de la función");
        if (isMethod) fn->SelfMut = (!fn->Params.empty() && fn->Params[0].IsSelf &&
                                     fn->Params[0].Mut);
        return fn;
    }
    std::unique_ptr<TypeDecl> ParseTypeDecl() {
        auto td = std::make_unique<TypeDecl>();
        Expect(TT::KwType, "`type`");
        const Token& nt = Expect(TT::Ident, "el nombre del struct");
        td->Name = nt.Text;
        td->Sp = nt.Sp;
        Expect(TT::Colon, "`:` para abrir el cuerpo del struct");
        Expect(TT::Newline, "un salto de línea (el cuerpo va con sangría)");
        if (!Accept(TT::Indent)) {
            Fail("un cuerpo con sangría para el struct");
            return td;
        }
        while (!Is(TT::Dedent) && !IsEof()) {
            if (Accept(TT::Newline)) continue;
            if (Is(TT::KwFn)) {
                td->Methods.push_back(ParseFn(true, td->Name));
            } else if (Is(TT::Ident)) {
                FieldDecl f;
                f.Sp = Cur().Sp;
                f.Name = Cur().Text;
                Advance();
                Expect(TT::Colon, "`:` con el tipo del campo (X: float)");
                f.Ty = ParseTypeAnn(false);
                if (Accept(TT::Eq)) {
                    f.HasDefault = true;
                    f.Default = ParseExpr();
                }
                td->Fields.push_back(std::move(f));
                if (!Is(TT::Dedent) && !IsEof())
                    Expect(TT::Newline, "un salto de línea (un campo por línea)");
            } else {
                Fail("un campo (`X: int`) o un método (`fn ...`)");
                SyncLine();
                continue;
            }
        }
        Accept(TT::Dedent);
        return td;
    }
    std::unique_ptr<EnumDecl> ParseEnum() {
        auto ed = std::make_unique<EnumDecl>();
        Expect(TT::KwEnum, "`enum`");
        const Token& nt = Expect(TT::Ident, "el nombre del enum");
        ed->Name = nt.Text;
        ed->Sp = nt.Sp;
        Expect(TT::Colon, "`:` para abrir el cuerpo del enum");
        Expect(TT::Newline, "un salto de línea (el cuerpo va con sangría)");
        if (!Accept(TT::Indent)) {
            Fail("un cuerpo con sangría para el enum");
            return ed;
        }
        while (!Is(TT::Dedent) && !IsEof()) {
            if (Accept(TT::Newline)) continue;
            const Token& vt = Expect(TT::Ident, "el nombre de una variante");
            EnumVariant v;
            v.Name = vt.Text;
            v.Sp = vt.Sp;
            if (Accept(TT::LParen)) {
                while (true) {
                    FieldDecl f;
                    f.Sp = Cur().Sp;
                    const Token& ft = Expect(TT::Ident, "el nombre del campo de la variante");
                    f.Name = ft.Text;
                    Expect(TT::Colon, "`:` con el tipo del campo (Id: int)");
                    f.Ty = ParseTypeAnn(false);
                    v.Fields.push_back(std::move(f));
                    if (!Accept(TT::Comma)) break;
                }
                Expect(TT::RParen, "`)` que cierra los campos de la variante");
            }
            ed->Variants.push_back(std::move(v));
            if (!Is(TT::Dedent) && !IsEof())
                Expect(TT::Newline, "un salto de línea (una variante por línea)");
        }
        Accept(TT::Dedent);
        return ed;
    }
    void ParseForeign(Program* p) {
        Advance();
        Expect(TT::Colon, "`:` para abrir el bloque foreign");
        Expect(TT::Newline, "un salto de línea (el bloque va con sangría)");
        if (!Accept(TT::Indent)) {
            Fail("un bloque con sangría para foreign");
            return;
        }
        while (!Is(TT::Dedent) && !IsEof()) {
            if (Accept(TT::Newline)) continue;
            if (Is(TT::KwFn)) {
                p->Fns.push_back(ParseFn(false, "", true));
            } else if (Is(TT::Ident) && Cur().Text == "include") {
                Advance();
                std::string header;
                if (Accept(TT::Lt)) {
                    const Token& h = Expect(TT::Ident, "el nombre del header tras `<`");
                    header = "<" + h.Text + ">";
                    Expect(TT::Gt, "`>` que cierra el header");
                } else {
                    const Token& s = Expect(TT::Str,
                        "el header: <cmath> o \"cmath\"");
                    header = s.Text;
                }
                p->ForeignIncludes.push_back(header);
                if (!Is(TT::Dedent) && !IsEof())
                    Expect(TT::Newline, "un salto de línea (un include por línea)");
            } else {
                Fail("`include \"cabecera\"` o `fn Nombre(args) -> ret`");
                SyncLine();
                continue;
            }
        }
        Accept(TT::Dedent);
    }
    Block ParseBlock(const std::string& ctx) {
        Block b;
        b.Sp = Cur().Sp;
        Expect(TT::Colon, std::string("`:` para abrir ") + ctx);
        b = ParseIndentedBody(ctx);
        return b;
    }
    Block ParseIndentedBody(const std::string& ctx) {
        Block b;
        b.Sp = Cur().Sp;
        Expect(TT::Newline, "un salto de línea (el bloque va en las líneas siguientes)");
        if (!Accept(TT::Indent)) {
            Fail(std::string("un bloque con sangría (el contenido de ") + ctx +
                 " va una línea más adentro)");
            return b;
        }
        while (!Is(TT::Dedent) && !IsEof()) {
            if (Accept(TT::Newline)) continue;
            StmtP s = ParseStmt();
            if (s) b.Stmts.push_back(std::move(s));
        }
        Accept(TT::Dedent);
        return b;
    }
    StmtP ParseStmt() {
        switch (Cur().T) {
            case TT::KwLet:
            case TT::KwVar:
                return ParseLet();
            case TT::KwIf:
                return ParseIf();
            case TT::KwWhile:
                return ParseWhile();
            case TT::KwFor:
                return ParseFor();
            case TT::KwMatch:
                return ParseMatch();
            case TT::KwReturn:
                return ParseReturn();
            case TT::KwBreak: {
                auto s = std::make_unique<Stmt>(Stmt::Kind::Break);
                s->Sp = Cur().Sp;
                Advance();
                return s;
            }
            case TT::KwContinue: {
                auto s = std::make_unique<Stmt>(Stmt::Kind::Continue);
                s->Sp = Cur().Sp;
                Advance();
                return s;
            }
            case TT::KwPass: {
                auto s = std::make_unique<Stmt>(Stmt::Kind::Pass);
                s->Sp = Cur().Sp;
                Advance();
                return s;
            }
            default:
                return ParseExprOrAssign();
        }
    }
    StmtP ParseLet() {
        auto s = std::make_unique<LetStmt>();
        s->Mutable = (Cur().T == TT::KwVar);
        s->Sp = Cur().Sp;
        Advance();
        if (Accept(TT::LParen)) {
            s->Destructure = true;
            while (true) {
                const Token& n = Expect(TT::Ident, "un nombre dentro de la tupla");
                s->Names.push_back(n.Text);
                if (!Accept(TT::Comma)) break;
            }
            Expect(TT::RParen, "`)` que cierra la destructuración");
            if (s->Names.size() < 2) {
                Diag d = Rep.Err("E0201", s->Sp,
                    "para desestructurar se necesitan dos o más nombres");
                d.Helps.push_back("si solo quieres un nombre, escribe `let X = ...` sin paréntesis");
                Rep.Emit(d);
            }
        } else {
            const Token& n = Expect(TT::Ident, "el nombre de la variable");
            s->Names.push_back(n.Text);
        }
        if (Accept(TT::Colon)) {
            s->HasTy = true;
            s->Ty = ParseTypeAnnBase(false);
        }
        Expect(TT::Eq, "`=` para dar un valor inicial (todo se inicializa)");
        s->Init = ParseExpr();
        return s;
    }
    StmtP ParseIf() {
        auto s = std::make_unique<IfStmt>();
        s->Sp = Cur().Sp;
        Advance();
        s->Cond = ParseExpr();
        s->Then = ParseBlock("el bloque del `if`");
        while (Is(TT::KwElif)) {
            Advance();
            IfStmt::Branch br;
            br.Cond = ParseExpr();
            br.Body = ParseBlock("el bloque del `elif`");
            s->Elifs.push_back(std::move(br));
        }
        if (Accept(TT::KwElse)) {
            s->HasElse = true;
            s->Else = ParseBlock("el bloque del `else`");
        }
        return s;
    }
    StmtP ParseWhile() {
        auto s = std::make_unique<WhileStmt>();
        s->Sp = Cur().Sp;
        Advance();
        s->Cond = ParseExpr();
        s->Body = ParseBlock("el bloque del `while`");
        return s;
    }
    StmtP ParseFor() {
        auto s = std::make_unique<ForStmt>();
        s->Sp = Cur().Sp;
        Advance();
        const Token& v = Expect(TT::Ident, "el nombre de la variable del bucle");
        s->Var = v.Text;
        Expect(TT::KwIn, "`in` (así se escribe un bucle: `for I in 0..10`)");
        s->Low = ParseExpr();
        if (Accept(TT::DotDot)) {
            s->IsRange = true;
            s->High = ParseExpr();
        }
        s->Body = ParseBlock("el bloque del `for`");
        return s;
    }
    StmtP ParseMatch() {
        auto s = std::make_unique<MatchStmt>();
        s->Sp = Cur().Sp;
        Advance();
        s->Subject = ParseExpr();
        Expect(TT::Colon, "`:` para abrir el match");
        Expect(TT::Newline, "un salto de línea (los casos van con sangría)");
        if (!Accept(TT::Indent)) {
            Fail("al menos un `case` dentro del match");
            return s;
        }
        while (Accept(TT::Newline)) {}
        bool any = false;
        while (Is(TT::KwCase)) {
            any = true;
            Advance();
            MatchArm arm;
            arm.Pat = ParsePattern();
            Expect(TT::Colon, "`:` tras el patrón");
            arm.Body = ParseIndentedBody("el cuerpo del `case`");
            s->Arms.push_back(std::move(arm));
            while (Accept(TT::Newline)) {}
        }
        if (!any) Fail("`case` (un match necesita al menos un caso)");
        Accept(TT::Dedent);
        return s;
    }
    Pattern ParsePattern() {
        Pattern p;
        p.Sp = Cur().Sp;
        TT t = Cur().T;
        if (t == TT::Int) {
            auto e = std::make_unique<IntLitExpr>();
            e->Val = Cur().IntVal;
            e->Sp = Cur().Sp;
            Advance();
            p.k = Pattern::K::Literal;
            p.Lit = std::move(e);
            return p;
        }
        if (t == TT::Float) {
            auto e = std::make_unique<FloatLitExpr>();
            e->Val = Cur().FloatVal;
            e->Sp = Cur().Sp;
            Advance();
            p.k = Pattern::K::Literal;
            p.Lit = std::move(e);
            return p;
        }
        if (t == TT::Str) {
            auto e = std::make_unique<StrLitExpr>();
            StrPart part;
            part.IsText = true;
            part.Text = Cur().Text;
            e->Parts.push_back(std::move(part));
            e->Sp = Cur().Sp;
            Advance();
            p.k = Pattern::K::Literal;
            p.Lit = std::move(e);
            return p;
        }
        if (t == TT::KwTrue || t == TT::KwFalse) {
            auto e = std::make_unique<BoolLitExpr>();
            e->Val = (t == TT::KwTrue);
            e->Sp = Cur().Sp;
            Advance();
            p.k = Pattern::K::Literal;
            p.Lit = std::move(e);
            return p;
        }
        if (t == TT::Minus && Peek().T == TT::Int) {
            Span sp = Cur().Sp;
            Advance();
            auto e = std::make_unique<IntLitExpr>();
            e->Val = -Cur().IntVal;
            e->Sp = sp;
            Advance();
            p.k = Pattern::K::Literal;
            p.Lit = std::move(e);
            return p;
        }
        if (t == TT::Ident) {
            p.Name = Cur().Text;
            Advance();
            if (p.Name == "_") {
                p.k = Pattern::K::Wildcard;
                return p;
            }
            if (Is(TT::Dot)) {
                Advance();
                const Token& s = Expect(TT::Ident, "el nombre de la variante tras `.`");
                p.Qualified = true;
                p.Name += "." + s.Text;
            }
            if (Accept(TT::LParen)) {
                while (true) {
                    const Token& b = Expect(TT::Ident,
                        "un nombre para enlazar el campo (o `_` si no lo usas)");
                    p.Bindings.push_back(b.Text);
                    if (!Accept(TT::Comma)) break;
                }
                Expect(TT::RParen, "`)` que cierra los enlaces del patrón");
            }
            p.k = Pattern::K::Variant;
            return p;
        }
        Fail("un patrón (Variante(X, Y), Enum.Variante, un literal o `_`)");
        SyncLine();
        return p;
    }
    StmtP ParseReturn() {
        auto s = std::make_unique<ReturnStmt>();
        s->Sp = Cur().Sp;
        Advance();
        if (!Is(TT::Newline) && !IsEof() && !Is(TT::Dedent)) s->Value = ParseExpr();
        return s;
    }
    StmtP ParseExprOrAssign() {
        ExprP e = ParseExpr();
        if (!e) { SyncLine(); return nullptr; }
        TT t = Cur().T;
        if (t == TT::Eq || t == TT::PlusEq || t == TT::MinusEq || t == TT::StarEq ||
            t == TT::SlashEq) {
            if (e->K != Expr::Kind::Ident && e->K != Expr::Kind::Index &&
                e->K != Expr::Kind::Member) {
                Diag d = Rep.Err("E0202", e->Sp,
                    "aquí no se puede asignar");
                d.Helps.push_back(
                    "puedes asignar a una variable (X), a una posición (A[I]) o a un campo (P.X)");
                Rep.Emit(d);
            }
            auto s = std::make_unique<AssignStmt>();
            s->Sp = Cur().Sp;
            switch (t) {
                case TT::Eq: s->Op = "="; break;
                case TT::PlusEq: s->Op = "+="; break;
                case TT::MinusEq: s->Op = "-="; break;
                case TT::StarEq: s->Op = "*="; break;
                default: s->Op = "/="; break;
            }
            Advance();
            s->Target = std::move(e);
            s->Value = ParseExpr();
            return s;
        }
        auto s = std::make_unique<ExprStmt>();
        s->Sp = e->Sp;
        s->Sub = std::move(e);
        return s;
    }
    ExprP ParseExpr() { return ParseOr(); }
    ExprP ParseOr() {
        ExprP l = ParseAnd();
        while (Is(TT::KwOr)) {
            Span sp = Cur().Sp;
            Advance();
            ExprP r = ParseAnd();
            auto b = std::make_unique<BinaryExpr>();
            b->Op = "or";
            b->Sp = sp;
            b->L = std::move(l);
            b->R = std::move(r);
            l = std::move(b);
        }
        return l;
    }
    ExprP ParseAnd() {
        ExprP l = ParseNot();
        while (Is(TT::KwAnd)) {
            Span sp = Cur().Sp;
            Advance();
            ExprP r = ParseNot();
            auto b = std::make_unique<BinaryExpr>();
            b->Op = "and";
            b->Sp = sp;
            b->L = std::move(l);
            b->R = std::move(r);
            l = std::move(b);
        }
        return l;
    }
    ExprP ParseNot() {
        if (Is(TT::KwNot)) {
            Span sp = Cur().Sp;
            Advance();
            auto u = std::make_unique<UnaryExpr>();
            u->Op = "not";
            u->Sp = sp;
            u->Sub = ParseNot();
            return u;
        }
        return ParseCmp();
    }
    ExprP ParseCmp() {
        ExprP l = ParseAdd();
        TT t = Cur().T;
        if (t == TT::EqEq || t == TT::NotEq || t == TT::Lt || t == TT::LtEq ||
            t == TT::Gt || t == TT::GtEq) {
            Span sp = Cur().Sp;
            std::string op;
            switch (t) {
                case TT::EqEq: op = "=="; break;
                case TT::NotEq: op = "!="; break;
                case TT::Lt: op = "<"; break;
                case TT::LtEq: op = "<="; break;
                case TT::Gt: op = ">"; break;
                default: op = ">="; break;
            }
            Advance();
            ExprP r = ParseAdd();
            auto b = std::make_unique<BinaryExpr>();
            b->Op = op;
            b->Sp = l ? l->Sp : sp;
            b->L = std::move(l);
            b->R = std::move(r);
            if (b->L && b->R)
                b->Sp.Len = b->R->Sp.Col + b->R->Sp.Len - b->L->Sp.Col;
            l = std::move(b);
            t = Cur().T;
            if (t == TT::EqEq || t == TT::NotEq || t == TT::Lt || t == TT::LtEq ||
                t == TT::Gt || t == TT::GtEq) {
                Diag d = Rep.Err("E0201", Cur().Sp,
                    "no se pueden encadenar comparaciones así");
                d.Helps.push_back("exprésalo con `and`: `if A < B and B < C`");
                Rep.Emit(d);
            }
        }
        return l;
    }
    ExprP ParseAdd() {
        ExprP l = ParseMul();
        while (Is(TT::Plus) || Is(TT::Minus)) {
            Span sp = Cur().Sp;
            std::string op = (Cur().T == TT::Plus) ? "+" : "-";
            Advance();
            ExprP r = ParseMul();
            auto b = std::make_unique<BinaryExpr>();
            b->Op = op;
            b->Sp = l ? l->Sp : sp;
            b->L = std::move(l);
            b->R = std::move(r);
            if (b->L && b->R)
                b->Sp.Len = b->R->Sp.Col + b->R->Sp.Len - b->L->Sp.Col;
            l = std::move(b);
        }
        return l;
    }
    ExprP ParseMul() {
        ExprP l = ParseUnary();
        while (Is(TT::Star) || Is(TT::Slash) || Is(TT::Percent)) {
            Span sp = Cur().Sp;
            std::string op = (Cur().T == TT::Star) ? "*"
                             : (Cur().T == TT::Slash) ? "/" : "%";
            Advance();
            ExprP r = ParseUnary();
            auto b = std::make_unique<BinaryExpr>();
            b->Op = op;
            b->Sp = l ? l->Sp : sp;
            b->L = std::move(l);
            b->R = std::move(r);
            if (b->L && b->R)
                b->Sp.Len = b->R->Sp.Col + b->R->Sp.Len - b->L->Sp.Col;
            l = std::move(b);
        }
        return l;
    }
    ExprP ParseUnary() {
        if (Is(TT::Minus)) {
            Span sp = Cur().Sp;
            Advance();
            auto u = std::make_unique<UnaryExpr>();
            u->Op = "-";
            u->Sp = sp;
            u->Sub = ParseUnary();
            return u;
        }
        return ParsePostfix();
    }
    ExprP ParsePostfix() {
        ExprP e = ParsePrimary();
        if (!e) return nullptr;
        while (true) {
            if (Is(TT::LParen)) {
                auto c = std::make_unique<CallExpr>();
                c->Sp = Cur().Sp;
                c->Callee = std::move(e);
                c->Args = ParseArgs();
                c->Sp.Len = (int)(Cur().Sp.Col - c->Sp.Col) + 1;
                e = std::move(c);
            } else if (Is(TT::LBracket)) {
                auto ix = std::make_unique<IndexExpr>();
                ix->Sp = Cur().Sp;
                ix->Base = std::move(e);
                Advance();
                ix->Idx = ParseExpr();
                Expect(TT::RBracket, "`]` que cierra el índice");
                e = std::move(ix);
            } else if (Is(TT::Dot)) {
                Advance();
                const Token& n = Expect(TT::Ident, "un campo o método tras `.`");
                auto m = std::make_unique<MemberExpr>();
                m->Sp = n.Sp;
                m->NameSp = n.Sp;
                m->Name = n.Text;
                m->Base = std::move(e);
                e = std::move(m);
            } else {
                break;
            }
        }
        return e;
    }
    std::vector<Arg> ParseArgs() {
        std::vector<Arg> args;
        Expect(TT::LParen, "`(`");
        if (!Is(TT::RParen)) {
            while (true) {
                Arg a;
                a.Sp = Cur().Sp;
                if (Accept(TT::Amp)) a.Borrowed = true;
                if (Is(TT::Ident) && Peek().T == TT::Colon && !a.Borrowed) {
                    a.Name = Cur().Text;
                    Advance();
                    Advance();
                }
                a.Value = ParseExpr();
                if (a.Value) args.push_back(std::move(a));
                if (Accept(TT::Comma)) {
                    if (Is(TT::RParen)) break;
                    continue;
                }
                break;
            }
        }
        Expect(TT::RParen, "`)` que cierra la llamada");
        return args;
    }
    ExprP ParsePrimary() {
        switch (Cur().T) {
            case TT::Int: {
                auto e = std::make_unique<IntLitExpr>();
                e->Val = Cur().IntVal;
                e->Sp = Cur().Sp;
                Advance();
                return e;
            }
            case TT::Float: {
                auto e = std::make_unique<FloatLitExpr>();
                e->Val = Cur().FloatVal;
                e->Sp = Cur().Sp;
                Advance();
                return e;
            }
            case TT::KwTrue:
            case TT::KwFalse: {
                auto e = std::make_unique<BoolLitExpr>();
                e->Val = (Cur().T == TT::KwTrue);
                e->Sp = Cur().Sp;
                Advance();
                return e;
            }
            case TT::Str: {
                Token tk = Cur();
                Advance();
                return ParseStringLiteral(tk);
            }
            case TT::Ident:
            case TT::KwSelf: {
                auto e = std::make_unique<IdentExpr>();
                e->Name = Cur().Text;
                e->Sp = Cur().Sp;
                Advance();
                return e;
            }
            case TT::LParen: {
                Span sp = Cur().Sp;
                Advance();
                if (Is(TT::RParen)) {
                    Diag d = Rep.Err("E0201", sp, "no existe la tupla vacía");
                    d.Helps.push_back("si querías una lista vacía, usa `[]` con su tipo anotado");
                    Rep.Emit(d);
                    Advance();
                    return nullptr;
                }
                ExprP first = ParseExpr();
                if (!first) return nullptr;
                if (Accept(TT::Comma)) {
                    auto t = std::make_unique<TupleLitExpr>();
                    t->Sp = sp;
                    t->Elems.push_back(std::move(first));
                    while (true) {
                        ExprP e = ParseExpr();
                        if (!e) break;
                        t->Elems.push_back(std::move(e));
                        if (Accept(TT::Comma)) {
                            if (Is(TT::RParen)) break;
                            continue;
                        }
                        break;
                    }
                    Expect(TT::RParen, "`)` que cierra la tupla");
                    return t;
                }
                Expect(TT::RParen, "`)` que cierra la expresión");
                return first;
            }
            case TT::LBracket: {
                Span sp = Cur().Sp;
                Advance();
                auto a = std::make_unique<ArrayLitExpr>();
                a->Sp = sp;
                if (!Is(TT::RBracket)) {
                    while (true) {
                        ExprP e = ParseExpr();
                        if (!e) break;
                        a->Elems.push_back(std::move(e));
                        if (Accept(TT::Comma)) {
                            if (Is(TT::RBracket)) break;
                            continue;
                        }
                        break;
                    }
                }
                Expect(TT::RBracket, "`]` que cierra el array");
                return a;
            }
            case TT::LBrace: {
                Span sp = Cur().Sp;
                Advance();
                auto m = std::make_unique<MapLitExpr>();
                m->Sp = sp;
                if (!Is(TT::RBrace)) {
                    while (true) {
                        MapPair p;
                        p.Sp = Cur().Sp;
                        p.Key = ParseExpr();
                        Expect(TT::Colon, "`:` entre la clave y el valor");
                        p.Val = ParseExpr();
                        if (p.Key && p.Val) m->Pairs.push_back(std::move(p));
                        if (Accept(TT::Comma)) {
                            if (Is(TT::RBrace)) break;
                            continue;
                        }
                        break;
                    }
                }
                Expect(TT::RBrace, "`}` que cierra el mapa");
                return m;
            }
            default:
                Fail("una expresión");
                return nullptr;
        }
    }
    ExprP ParseStringLiteral(const Token& tk) {
        auto s = std::make_unique<StrLitExpr>();
        s->Sp = tk.Sp;
        const std::string& raw = tk.Text;
        std::string lit;
        size_t i = 0;
        while (i < raw.size()) {
            char c = raw[i];
            if (c == '{') {
                if (i + 1 < raw.size() && raw[i + 1] == '{') { lit += '{'; i += 2; continue; }
                if (!lit.empty()) {
                    StrPart p;
                    p.IsText = true;
                    p.Text = lit;
                    s->Parts.push_back(std::move(p));
                    lit.clear();
                }
                int depth = 1;
                size_t j = i + 1;
                bool bad = false;
                while (j < raw.size()) {
                    if (raw[j] == '{') { depth++; j++; continue; }
                    if (raw[j] == '}') {
                        depth--;
                        if (depth == 0) break;
                        j++;
                        continue;
                    }
                    if (raw[j] == '"') {
                        j++;
                        while (j < raw.size() && raw[j] != '"') {
                            if (raw[j] == '\\' && j + 1 < raw.size()) j++;
                            j++;
                        }
                        if (j >= raw.size()) { bad = true; break; }
                        j++;
                        continue;
                    }
                    j++;
                }
                int braceAbsCol = tk.Sp.Col + 1 + (int)i;
                if (bad) {
                    Diag d = Rep.Err("E0102", Span(tk.Sp.Line, braceAbsCol, 1),
                        "un string dentro de la interpolación no está cerrado");
                    d.Helps.push_back("revisa las comillas dentro de `{ }`");
                    Rep.Emit(d);
                    break;
                }
                if (j >= raw.size()) {
                    Diag d = Rep.Err("E0102", Span(tk.Sp.Line, braceAbsCol, 1),
                        "no encuentro el `}` que cierra esta interpolación");
                    d.Helps.push_back("cada `{` dentro de un string se cierra con su `}`");
                    Rep.Emit(d);
                    break;
                }
                std::string content = raw.substr(i + 1, j - i - 1);
                if (Trim(content).empty()) {
                    Diag d = Rep.Err("E0201", Span(tk.Sp.Line, braceAbsCol, j - i + 1),
                        "no hay nada entre `{` y `}`");
                    d.Helps.push_back("dentro de las llaves va una expresión: \"{X + 1}\"");
                    Rep.Emit(d);
                    i = j + 1;
                    continue;
                }
                std::vector<Token> sub = LexSubstring(content, tk.Sp.Line, braceAbsCol + 1, Rep);
                ExprP e = ParseSubExpression(sub, Src, Rep);
                if (e) {
                    StrPart p;
                    p.IsText = false;
                    p.Sub = std::move(e);
                    s->Parts.push_back(std::move(p));
                    s->HasInterp = true;
                }
                i = j + 1;
                continue;
            }
            if (c == '}') {
                if (i + 1 < raw.size() && raw[i + 1] == '}') { lit += '}'; i += 2; continue; }
            }
            lit += c;
            i++;
        }
        if (!lit.empty()) {
            StrPart p;
            p.IsText = true;
            p.Text = lit;
            s->Parts.push_back(std::move(p));
        }
        return s;
    }
};
}
ExprP ParseSubExpression(const std::vector<Token>& toks, const SourceFile& src,
                         Reporter& rep) {
    Parser p(toks, src, rep);
    ExprP e = p.ParseExpr();
    if (e && !p.IsEof()) {
        Diag d = rep.Err("E0201", p.Cur().Sp,
            "no entendí la expresión dentro de la interpolación");
        d.Helps.push_back("dentro de `{ }` va una sola expresión: \"{X}\" o \"{Fib(N)}\"");
        rep.Emit(d);
        return nullptr;
    }
    return e;
}
ProgramP Parse(const std::vector<Token>& toks, const SourceFile& src, Reporter& rep) {
    Parser p(toks, src, rep);
    return p.ParseProgram();
}
}
