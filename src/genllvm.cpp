#include "genllvm.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
namespace sylt {
namespace {

class G {
public:
    ProgramP& Prog;
    GenOptions Opt;
    std::ostringstream Ts, Gs, Ds, Bs;
    int RegN = 0, LblN = 0, StrN = 0;
    std::vector<std::pair<std::string, std::string>> Blocks;
    std::vector<char> Closed;
    std::unordered_map<std::string, TypeDecl*> TypeByName;
    std::unordered_map<std::string, EnumDecl*> EnumByName;
    std::unordered_map<std::string, FnDecl*> FnByName;
    std::unordered_map<std::string, std::string> QualNames;
    std::unordered_map<std::string, std::string> VarPtr;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> EnumFieldPos;
    std::vector<std::pair<std::string, std::string>> Undo;
    std::vector<std::pair<std::string, std::string>> LoopStack;
    Type RetType;
    std::string CurModule;

    explicit G(ProgramP& prog, const GenOptions& opt) : Prog(prog), Opt(opt) {
        for (auto& td : Prog->Types) {
            std::string key = Key(td->Module, td->Name);
            TypeByName[key] = td.get();
            if (!td->Module.empty()) QualNames[td->Module + "." + td->Name] = key;
        }
        for (auto& ed : Prog->Enums) {
            std::string key = Key(ed->Module, ed->Name);
            EnumByName[key] = ed.get();
            if (!ed->Module.empty()) QualNames[ed->Module + "." + ed->Name] = key;
        }
        for (auto& fd : Prog->Fns) FnByName[Key(fd->Module, fd->Name)] = fd.get();
    }

    static std::string Key(const std::string& mod, const std::string& n) {
        if (mod.empty()) return n;
        std::string m = mod;
        for (auto& c : m)
            if (c == '.') c = '_';
        return m + "__" + n;
    }
    std::string N() { return "%v" + std::to_string(RegN++); }
    std::string Lbl(const std::string& hint) {
        return "bb" + std::to_string(LblN++) + "." + hint;
    }
    void Open(const std::string& l) {
        Blocks.push_back({l, ""});
        Closed.push_back(0);
    }
    void I(const std::string& s) {
        if (!Closed.empty() && Closed.back()) Open(Lbl("dead"));
        Blocks.back().second += "  " + s + "\n";
    }
    void Term(const std::string& s) {
        I(s);
        if (!Closed.empty()) Closed.back() = 1;
    }

    Type TypeOf(const TypeAnn& a) {
        switch (a.k) {
            case TypeAnn::K::None: return Type::Err();
            case TypeAnn::K::Prim:
                if (a.Name == "int") return Type::Int();
                if (a.Name == "float") return Type::Float();
                if (a.Name == "bool") return Type::Bool();
                if (a.Name == "string") return Type::Str();
                if (a.Name == "void") return Type::Void();
                return Type::Err();
            case TypeAnn::K::Ident: {
                if (a.Name.find('.') != std::string::npos) {
                    auto it = QualNames.find(a.Name);
                    return Type::StructOf(it != QualNames.end() ? it->second : a.Name);
                }
                if (!CurModule.empty()) {
                    std::string k = CurModule + "__" + a.Name;
                    if (TypeByName.count(k) || EnumByName.count(k)) return Type::StructOf(k);
                }
                return Type::StructOf(a.Name);
            }
            case TypeAnn::K::Array: return Type::ArrayOf(TypeOf(a.Sub[0]));
            case TypeAnn::K::Map: return Type::MapOf(TypeOf(a.Sub[0]), TypeOf(a.Sub[1]));
            case TypeAnn::K::Tuple: {
                std::vector<Type> ms;
                for (auto& s : a.Sub) ms.push_back(TypeOf(s));
                return Type::TupleOf(std::move(ms));
            }
            case TypeAnn::K::Borrow: return Type::BorrowOf(TypeOf(a.Sub[0]), a.Mut);
        }
        return Type::Err();
    }

    std::string LT(const Type& t, const Span& sp) {
        switch (t.K) {
            case TK::Int: return "i64";
            case TK::Float: return "double";
            case TK::Bool: return "i1";
            case TK::Str: return "ptr";
            case TK::Void: return "void";
            case TK::Struct: return "%" + t.Name;
            case TK::Enum: return "%" + t.Name;
            case TK::Borrow: return LT(t.Pointee(), sp);
            case TK::Array:
                throw Unsupported{sp, "los arrays no están soportados todavía por el backend llvm"};
            case TK::Map:
                throw Unsupported{sp, "los mapas no están soportados todavía por el backend llvm"};
            case TK::Tuple:
                throw Unsupported{sp, "las tuplas no están soportadas todavía por el backend llvm"};
            default:
                throw Unsupported{sp, "este tipo no está soportado por el backend llvm"};
        }
    }

    void LetBind(const std::string& name, const std::string& ptr) {
        Undo.push_back({name, VarPtr.count(name) ? VarPtr[name] : std::string()});
        VarPtr[name] = ptr;
    }
    void ScopeRestore(size_t mark) {
        while (Undo.size() > mark) {
            auto& e = Undo.back();
            if (e.second.empty()) VarPtr.erase(e.first);
            else VarPtr[e.first] = e.second;
            Undo.pop_back();
        }
    }

    int FieldIndex(TypeDecl* td, const std::string& n) {
        for (size_t i = 0; i < td->Fields.size(); i++)
            if (td->Fields[i].Name == n) return (int)i;
        return -1;
    }
    int VariantIndex(EnumDecl* ed, const std::string& n) {
        for (size_t i = 0; i < ed->Variants.size(); i++)
            if (ed->Variants[i].Name == n) return (int)i;
        return -1;
    }

    std::string Esc(const std::string& s) {
        static const char* hex = "0123456789ABCDEF";
        std::string o;
        for (unsigned char c : s) {
            if (c >= 32 && c <= 126 && c != '"' && c != '\\') o += (char)c;
            else {
                o += '\\';
                o += hex[c >> 4];
                o += hex[c & 15];
            }
        }
        return o;
    }
    std::string StrConst(const std::string& text) {
        std::string name = "@.s" + std::to_string(StrN++);
        Gs << name << " = private unnamed_addr constant [" << (text.size() + 1)
           << " x i8] c\"" << Esc(text) << "\\00\"\n";
        return name;
    }
    std::string StrLit(const std::string& text) {
        std::string g = StrConst(text);
        std::string r = N();
        I(r + " = call ptr @sylt.str_lit(ptr " + g + ")");
        return r;
    }

    std::string FloatLit(double v) {
        if (std::isnan(v) || std::isinf(v)) {
            uint64_t b;
            std::memcpy(&b, &v, 8);
            char h[32];
            std::snprintf(h, sizeof(h), "0x%016llX", (unsigned long long)b);
            return h;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        std::string s(buf);
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) return s + ".0";
        if (s.find('.') == std::string::npos) {
            size_t p = s.find('e');
            s.insert(p, ".0");
        }
        return s;
    }

    struct RVal {
        std::string T;
        std::string V;
        Type Ty;
    };
    RVal CastVal(RVal v, const Type& to) {
        if (v.Ty.IsInt() && to.IsFloat()) {
            std::string r = N();
            I(r + " = sitofp i64 " + v.V + " to double");
            return {"double", r, to};
        }
        return v;
    }
    RVal MatchNumeric(RVal l, RVal r) {
        if (l.Ty.IsInt() && r.Ty.IsFloat()) return CastVal(l, r.Ty);
        if (l.Ty.IsFloat() && r.Ty.IsInt()) return l;
        return l;
    }
    std::string ToStrVal(const std::string& reg, const Type& t) {
        if (t.IsStr()) return reg;
        std::string r = N();
        if (t.IsInt()) I(r + " = call ptr @sylt.i64_str(i64 " + reg + ")");
        else if (t.IsFloat()) I(r + " = call ptr @sylt.f64_str(double " + reg + ")");
        else if (t.IsBool()) I(r + " = call ptr @sylt.bool_str(i1 " + reg + ")");
        else if (t.IsStruct() || t.IsEnum()) {
            std::string tmp = N();
            I(tmp + " = alloca %" + t.Name);
            I("store %" + t.Name + " " + reg + ", ptr " + tmp);
            I(r + " = call ptr @sylt.str_of__" + t.Name + "(ptr " + tmp + ")");
        } else throw Unsupported{Span(), "no puedo convertir este valor a texto en el backend llvm"};
        return r;
    }

    struct LVal {
        std::string Ptr;
        Type Ty;
    };
    LVal GenLval(Expr* e) {
        if (e->K == Expr::Kind::Ident) {
            auto* id = static_cast<IdentExpr*>(e);
            auto it = VarPtr.find(id->Name);
            if (it == VarPtr.end())
                throw Unsupported{id->Sp, "variable no encontrada en el backend llvm"};
            return {it->second, id->Ty};
        }
        if (e->K == Expr::Kind::Member) {
            auto* m = static_cast<MemberExpr*>(e);
            Type bt = m->Base->Ty.Pointee();
            if (!bt.IsStruct())
                throw Unsupported{m->Sp, "no puedo acceder a campos de este valor en el backend llvm"};
            LVal b = GenLval(m->Base.get());
            auto f = TypeByName.find(bt.Name);
            if (f == TypeByName.end())
                throw Unsupported{m->Sp, "tipo desconocido en el backend llvm"};
            int idx = FieldIndex(f->second, m->Name);
            std::string r = N();
            I(r + " = getelementptr inbounds %" + bt.Name + ", ptr " + b.Ptr +
              ", i32 0, i32 " + std::to_string(idx));
            return {r, m->Ty};
        }
        if (e->K == Expr::Kind::Call) {
            RVal v = GenExpr(e);
            std::string t = N();
            I(t + " = alloca " + v.T);
            I("store " + v.T + " " + v.V + ", ptr " + t);
            return {t, e->Ty};
        }
        throw Unsupported{e->Sp, "no puedo asignar a esta expresión en el backend llvm"};
    }

    RVal GenExpr(Expr* e) {
        switch (e->K) {
            case Expr::Kind::IntLit:
                return {"i64", std::to_string(static_cast<IntLitExpr*>(e)->Val), e->Ty};
            case Expr::Kind::FloatLit:
                return {"double", FloatLit(static_cast<FloatLitExpr*>(e)->Val), e->Ty};
            case Expr::Kind::BoolLit:
                return {"i1", static_cast<BoolLitExpr*>(e)->Val ? "true" : "false", e->Ty};
            case Expr::Kind::StrLit: {
                auto* s = static_cast<StrLitExpr*>(e);
                std::string acc;
                bool first = true;
                for (auto& p : s->Parts) {
                    std::string piece;
                    if (p.IsText) piece = StrLit(p.Text);
                    else {
                        RVal sv = GenExpr(p.Sub.get());
                        piece = ToStrVal(sv.V, sv.Ty);
                    }
                    if (first) acc = piece;
                    else {
                        std::string t = N();
                        I(t + " = call ptr @sylt.str_concat(ptr " + acc + ", ptr " + piece + ")");
                        acc = t;
                    }
                    first = false;
                }
                if (first) acc = StrLit("");
                return {"ptr", acc, e->Ty};
            }
            case Expr::Kind::Ident: {
                auto* id = static_cast<IdentExpr*>(e);
                auto it = VarPtr.find(id->Name);
                if (it == VarPtr.end())
                    throw Unsupported{id->Sp, "variable no encontrada en el backend llvm"};
                std::string lt = LT(id->Ty, id->Sp);
                std::string r = N();
                I(r + " = load " + lt + ", ptr " + it->second);
                return {lt, r, id->Ty};
            }
            case Expr::Kind::Unary: {
                auto* u = static_cast<UnaryExpr*>(e);
                RVal v = GenExpr(u->Sub.get());
                std::string r = N();
                if (u->Op == "-") {
                    if (u->Ty.IsInt()) I(r + " = sub i64 0, " + v.V);
                    else I(r + " = fneg double " + v.V);
                } else {
                    I(r + " = xor i1 " + v.V + ", true");
                }
                return {LT(u->Ty, u->Sp), r, u->Ty};
            }
            case Expr::Kind::Binary: {
                auto* b = static_cast<BinaryExpr*>(e);
                const std::string& op = b->Op;
                if (op == "and" || op == "or") return GenLogic(b);
                RVal l = GenExpr(b->L.get());
                RVal r = GenExpr(b->R.get());
                if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" ||
                    op == "!=") {
                    l = MatchNumeric(l, r);
                    r = MatchNumeric(r, l);
                    std::string c = GenCompare(op, l, r);
                    return {"i1", c, e->Ty};
                }
                if (b->Ty.IsFloat()) {
                    l = CastVal(l, b->Ty);
                    r = CastVal(r, b->Ty);
                }
                std::string lt = LT(b->Ty, b->Sp);
                std::string out = N();
                bool isInt = b->Ty.IsInt();
                bool isStr = b->Ty.IsStr();
                if (isStr && op == "+") {
                    I(out + " = call ptr @sylt.str_concat(ptr " + l.V + ", ptr " + r.V + ")");
                } else if (isInt) {
                    std::string ins = (op == "+") ? "add i64" : (op == "-") ? "sub i64"
                                             : (op == "*") ? "mul i64"
                                                           : "";
                    if (op == "/") {
                        if (!Opt.Fast) {
                            std::string zc = N();
                            I(zc + " = icmp eq i64 " + r.V + ", 0");
                            std::string bad = Lbl("div0"), ok = Lbl("divok");
                            Term("br i1 " + zc + ", label %" + bad + ", label %" + ok);
                            Open(bad);
                            I("call void @sylt.rt_div0()");
                            I("unreachable");
                            Open(ok);
                        }
                        ins = "sdiv i64";
                    }
                    if (op == "%") {
                        if (!Opt.Fast) {
                            std::string zc = N();
                            I(zc + " = icmp eq i64 " + r.V + ", 0");
                            std::string bad = Lbl("mod0"), ok = Lbl("modok");
                            Term("br i1 " + zc + ", label %" + bad + ", label %" + ok);
                            Open(bad);
                            I("call void @sylt.rt_mod0()");
                            I("unreachable");
                            Open(ok);
                        }
                        ins = "srem i64";
                    }
                    I(out + " = " + ins + " " + l.V + ", " + r.V);
                } else {
                    std::string ins = (op == "+") ? "fadd double" : (op == "-") ? "fsub double"
                                                  : (op == "*") ? "fmul double"
                                                                : "fdiv double";
                    I(out + " = " + ins + " " + l.V + ", " + r.V);
                }
                return {lt, out, b->Ty};
            }
            case Expr::Kind::Call:
                return GenCall(static_cast<CallExpr*>(e));
            case Expr::Kind::Index:
                throw Unsupported{e->Sp,
                                  "los índices de colecciones no están soportados todavía "
                                  "por el backend llvm"};
            case Expr::Kind::Member: {
                auto* m = static_cast<MemberExpr*>(e);
                if (m->IsEnumUnit) {
                    auto ed = EnumByName.find(m->EnumKey);
                    if (ed == EnumByName.end())
                        throw Unsupported{m->Sp, "enum desconocido en el backend llvm"};
                    int vi = VariantIndex(ed->second, m->Name);
                    std::string t = N();
                    I(t + " = alloca %" + m->EnumKey);
                    I("store %" + m->EnumKey + " zeroinitializer, ptr " + t);
                    std::string tp = N();
                    I(tp + " = getelementptr inbounds %" + m->EnumKey + ", ptr " + t +
                      ", i32 0, i32 0");
                    I("store i64 " + std::to_string(vi) + ", ptr " + tp);
                    std::string r = N();
                    I(r + " = load %" + m->EnumKey + ", ptr " + t);
                    return {"%" + m->EnumKey, r, e->Ty};
                }
                LVal lv = GenLval(e);
                std::string lt = LT(e->Ty, e->Sp);
                std::string r = N();
                I(r + " = load " + lt + ", ptr " + lv.Ptr);
                return {lt, r, e->Ty};
            }
            case Expr::Kind::ArrayLit:
                throw Unsupported{e->Sp, "los arrays no están soportados todavía por el backend llvm"};
            case Expr::Kind::MapLit:
                throw Unsupported{e->Sp, "los mapas no están soportados todavía por el backend llvm"};
            case Expr::Kind::TupleLit:
                throw Unsupported{e->Sp, "las tuplas no están soportadas todavía por el backend llvm"};
        }
        throw Unsupported{e->Sp, "expresión no soportada por el backend llvm"};
    }

    RVal GenLogic(BinaryExpr* b) {
        bool isAnd = (b->Op == "and");
        std::string t = N();
        I(t + " = alloca i1");
        RVal l = GenExpr(b->L.get());
        I("store i1 " + l.V + ", ptr " + t);
        std::string rhs = Lbl("rhs"), shortc = Lbl("short"), merge = Lbl("merge");
        if (isAnd) Term("br i1 " + l.V + ", label %" + rhs + ", label %" + shortc);
        else Term("br i1 " + l.V + ", label %" + shortc + ", label %" + rhs);
        Open(shortc);
        I("store i1 " + std::string(isAnd ? "false" : "true") + ", ptr " + t);
        Term("br label %" + merge);
        Open(rhs);
        RVal r = GenExpr(b->R.get());
        I("store i1 " + r.V + ", ptr " + t);
        Term("br label %" + merge);
        Open(merge);
        std::string v = N();
        I(v + " = load i1, ptr " + t);
        return {"i1", v, b->Ty};
    }

    std::string GenCompare(const std::string& op, RVal& l, RVal& r) {
        if (l.Ty.IsStr()) {
            std::string c = N();
            I(c + " = call i32 @sylt.str_cmp(ptr " + l.V + ", ptr " + r.V + ")");
            std::string out = N();
            if (op == "==") I(out + " = icmp eq i32 " + c + ", 0");
            else if (op == "!=") I(out + " = icmp ne i32 " + c + ", 0");
            else if (op == "<") I(out + " = icmp slt i32 " + c + ", 0");
            else if (op == "<=") I(out + " = icmp sle i32 " + c + ", 0");
            else if (op == ">") I(out + " = icmp sgt i32 " + c + ", 0");
            else I(out + " = icmp sge i32 " + c + ", 0");
            return out;
        }
        if (l.Ty.IsFloat()) {
            std::string o = (op == "<") ? "olt" : (op == "<=") ? "ole"
                              : (op == ">") ? "ogt"
                              : (op == ">=") ? "oge"
                              : (op == "==") ? "oeq"
                                             : "one";
            std::string out = N();
            I(out + " = fcmp " + o + " double " + l.V + ", " + r.V);
            return out;
        }
        std::string o = (op == "<") ? "slt" : (op == "<=") ? "sle"
                          : (op == ">") ? "sgt"
                          : (op == ">=") ? "sge"
                          : (op == "==") ? "eq"
                                         : "ne";
        std::string out = N();
        I(out + " = icmp " + o + " i64 " + l.V + ", " + r.V);
        return out;
    }

    std::vector<std::pair<std::string, std::string>> GenArgs(CallExpr* c) {
        std::vector<std::pair<std::string, std::string>> out;
        for (size_t i = 0; i < c->Args.size(); i++) {
            Arg& a = c->Args[i];
            bool castF = i < c->ArgCastFloat.size() && c->ArgCastFloat[i];
            Type at = a.Value->Ty;
            if (a.Borrowed || at.IsBorrow()) {
                LVal lv = GenLval(a.Value.get());
                out.push_back({"ptr", lv.Ptr});
                continue;
            }
            bool agg = at.IsStruct() || at.IsEnum();
            if (agg) {
                LVal lv = GenLval(a.Value.get());
                out.push_back({"ptr", lv.Ptr});
            } else {
                RVal v = GenExpr(a.Value.get());
                if (castF) {
                    std::string r = N();
                    I(r + " = sitofp i64 " + v.V + " to double");
                    v = {"double", r, Type::Float()};
                }
                out.push_back({v.T, v.V});
            }
        }
        return out;
    }

    RVal GenCall(CallExpr* c) {
        switch (c->Resolved) {
            case CallExpr::Target::Builtin: {
                const std::string& n = c->TargetName;
                if (n == "Print") {
                    RVal v = GenExpr(c->Args[0].Value.get());
                    std::string s = ToStrVal(v.V, v.Ty);
                    I("call void @sylt.print(ptr " + s + ")");
                    return {"void", "", Type::Void()};
                }
                if (n == "Str") {
                    RVal v = GenExpr(c->Args[0].Value.get());
                    std::string s = ToStrVal(v.V, v.Ty);
                    return {"ptr", s, Type::Str()};
                }
                if (n == "Sqrt") {
                    RVal v = GenExpr(c->Args[0].Value.get());
                    std::string r = N();
                    I(r + " = call double @llvm.sqrt.f64(double " + v.V + ")");
                    return {"double", r, Type::Float()};
                }
                if (n == "Pow") {
                    RVal l = GenExpr(c->Args[0].Value.get());
                    RVal r = GenExpr(c->Args[1].Value.get());
                    std::string o = N();
                    I(o + " = call double @llvm.pow.f64(double " + l.V + ", double " + r.V + ")");
                    return {"double", o, Type::Float()};
                }
                if (n == "Floor") {
                    RVal v = GenExpr(c->Args[0].Value.get());
                    std::string f = N();
                    I(f + " = call double @llvm.floor.f64(double " + v.V + ")");
                    std::string r = N();
                    I(r + " = fptosi double " + f + " to i64");
                    return {"i64", r, Type::Int()};
                }
                if (n == "Abs") {
                    RVal v = GenExpr(c->Args[0].Value.get());
                    std::string neg = N(), cond = N(), r = N();
                    if (v.Ty.IsInt()) {
                        I(neg + " = sub i64 0, " + v.V);
                        I(cond + " = icmp slt i64 " + v.V + ", 0");
                        I(r + " = select i1 " + cond + ", i64 " + neg + ", i64 " + v.V);
                        return {"i64", r, Type::Int()};
                    }
                    I(neg + " = fneg double " + v.V);
                    I(cond + " = fcmp olt double " + v.V + ", 0.0");
                    I(r + " = select i1 " + cond + ", double " + neg + ", double " + v.V);
                    return {"double", r, Type::Float()};
                }
                if (n == "Min" || n == "Max") {
                    RVal l = GenExpr(c->Args[0].Value.get());
                    RVal r = GenExpr(c->Args[1].Value.get());
                    std::string cond = N(), out = N();
                    if (l.Ty.IsFloat()) {
                        if (n == "Min") I(cond + " = fcmp olt double " + l.V + ", " + r.V);
                        else I(cond + " = fcmp ogt double " + l.V + ", " + r.V);
                        I(out + " = select i1 " + cond + ", double " + l.V + ", double " + r.V);
                        return {"double", out, Type::Float()};
                    }
                    if (n == "Min") I(cond + " = icmp slt i64 " + l.V + ", " + r.V);
                    else I(cond + " = icmp sgt i64 " + l.V + ", " + r.V);
                    I(out + " = select i1 " + cond + ", i64 " + l.V + ", i64 " + r.V);
                    return {"i64", out, Type::Int()};
                }
                if (n == "Clone") return GenExpr(c->Args[0].Value.get());
                if (n == "TiempoMs")
                    throw Unsupported{c->Sp,
                                      "TiempoMs no está soportado todavía por el backend llvm"};
                throw Unsupported{c->Sp, "función integrada no soportada por el backend llvm"};
            }
            case CallExpr::Target::Fn: {
                auto f = FnByName.find(c->TargetName);
                auto args = GenArgs(c);
                std::string ret = "void";
                if (f != FnByName.end()) {
                    CurModule = f->second->Module;
                    if (f->second->HasRet) ret = LT(TypeOf(f->second->Ret), c->Sp);
                    CurModule = "";
                } else if (c->Ty.K != TK::Void) {
                    ret = LT(c->Ty, c->Sp);
                }
                std::string sig = "";
                for (size_t i = 0; i < args.size(); i++) {
                    if (i) sig += ", ";
                    sig += args[i].first + " " + args[i].second;
                }
                if (ret == "void") {
                    I("call void @" + c->TargetName + "(" + sig + ")");
                    return {"void", "", Type::Void()};
                }
                std::string r = N();
                I(r + " = call " + ret + " @" + c->TargetName + "(" + sig + ")");
                return {ret, r, c->Ty};
            }
            case CallExpr::Target::EnumCtor: {
                auto ed = EnumByName.find(c->TargetName);
                if (ed == EnumByName.end())
                    throw Unsupported{c->Sp, "enum desconocido en el backend llvm"};
                int vi = VariantIndex(ed->second, c->VariantName);
                auto args = GenArgs(c);
                std::string t = N();
                I(t + " = alloca %" + c->TargetName);
                I("store %" + c->TargetName + " zeroinitializer, ptr " + t);
                std::string tp = N();
                I(tp + " = getelementptr inbounds %" + c->TargetName + ", ptr " + t +
                  ", i32 0, i32 0");
                I("store i64 " + std::to_string(vi) + ", ptr " + tp);
                EnumVariant& v = ed->second->Variants[vi];
                CurModule = ed->second->Module;
                for (size_t i = 0; i < args.size() && i < v.Fields.size(); i++) {
                    Type ft = TypeOf(v.Fields[i].Ty);
                    int slot = 1 + EnumFieldPos[c->TargetName][v.Fields[i].Name];
                    std::string fp = N();
                    I(fp + " = getelementptr inbounds %" + c->TargetName + ", ptr " + t +
                      ", i32 0, i32 " + std::to_string(slot));
                    RVal val = {"", "", ft};
                    if (i < c->ArgCastFloat.size() && c->ArgCastFloat[i]) {
                        std::string cv = N();
                        I(cv + " = sitofp i64 " + args[i].second + " to double");
                        val = {"double", cv, ft};
                    } else {
                        val = {args[i].first, args[i].second, ft};
                    }
                    I("store " + val.T + " " + val.V + ", ptr " + fp);
                }
                CurModule = "";
                std::string r = N();
                I(r + " = load %" + c->TargetName + ", ptr " + t);
                return {"%" + c->TargetName, r, c->Ty};
            }
            case CallExpr::Target::StructInit: {
                auto it = TypeByName.find(c->TargetName);
                if (it == TypeByName.end())
                    throw Unsupported{c->Sp, "struct desconocido en el backend llvm"};
                TypeDecl* td = it->second;
                CurModule = td->Module;
                bool named = !c->Args.empty() && !c->Args[0].Name.empty();
                std::vector<std::string> vals(td->Fields.size(), "");
                for (size_t i = 0; i < c->Args.size(); i++) {
                    Arg& a = c->Args[i];
                    size_t dst = i;
                    if (named) {
                        for (size_t j = 0; j < td->Fields.size(); j++)
                            if (td->Fields[j].Name == a.Name) dst = j;
                    }
                    if (dst >= vals.size()) continue;
                    RVal v = GenExpr(a.Value.get());
                    if (i < c->ArgCastFloat.size() && c->ArgCastFloat[i]) {
                        std::string cv = N();
                        I(cv + " = sitofp i64 " + v.V + " to double");
                        v = {"double", cv, Type::Float()};
                    }
                    vals[dst] = v.V;
                }
                std::string t = N();
                I(t + " = alloca %" + c->TargetName);
                for (size_t i = 0; i < td->Fields.size(); i++) {
                    Type ft = TypeOf(td->Fields[i].Ty);
                    std::string lt = LT(ft, td->Fields[i].Sp);
                    std::string fp = N();
                    I(fp + " = getelementptr inbounds %" + c->TargetName + ", ptr " + t +
                      ", i32 0, i32 " + std::to_string(i));
                    if (!vals[i].empty()) {
                        I("store " + lt + " " + vals[i] + ", ptr " + fp);
                    } else if (td->Fields[i].HasDefault) {
                        RVal dv = GenExpr(td->Fields[i].Default.get());
                        dv = CastVal(dv, ft);
                        I("store " + lt + " " + dv.V + ", ptr " + fp);
                    } else {
                        I("store " + lt + " zeroinitializer, ptr " + fp);
                    }
                }
                CurModule = "";
                std::string r = N();
                I(r + " = load %" + c->TargetName + ", ptr " + t);
                return {"%" + c->TargetName, r, c->Ty};
            }
            case CallExpr::Target::Method: {
                auto* m = static_cast<MemberExpr*>(c->Callee.get());
                Type bt = m->Base->Ty.Pointee();
                if (bt.IsStr()) return GenStringMethod(c, m);
                if (bt.IsArray() || bt.IsMap())
                    throw Unsupported{c->Sp,
                                      "los métodos de colecciones no están soportados "
                                      "todavía por el backend llvm"};
                LVal recv = GenLval(m->Base.get());
                auto args = GenArgs(c);
                std::string full = bt.Name + "__" + c->TargetName;
                std::string ret = c->Ty.K == TK::Void ? "void" : LT(c->Ty, c->Sp);
                std::string sig = "ptr " + recv.Ptr;
                for (auto& a : args) sig += ", " + a.first + " " + a.second;
                if (ret == "void") {
                    I("call void @" + full + "(" + sig + ")");
                    return {"void", "", Type::Void()};
                }
                std::string r = N();
                I(r + " = call " + ret + " @" + full + "(" + sig + ")");
                return {ret, r, c->Ty};
            }
            default:
                throw Unsupported{c->Sp, "llamada no soportada por el backend llvm"};
        }
    }

    RVal GenStringMethod(CallExpr* c, MemberExpr* m) {
        RVal base = GenExpr(m->Base.get());
        const std::string& n = c->TargetName;
        if (n == "Len") {
            std::string lp = N();
            I(lp + " = getelementptr inbounds %sylt.str, ptr " + base.V + ", i32 0, i32 0");
            std::string r = N();
            I(r + " = load i64, ptr " + lp);
            return {"i64", r, Type::Int()};
        }
        if (n == "Contiene") {
            RVal sub = GenExpr(c->Args[0].Value.get());
            std::string dap = N(), dbp = N(), da = N(), db = N(), f = N(), r = N();
            I(dap + " = getelementptr inbounds %sylt.str, ptr " + base.V + ", i32 0, i32 1");
            I(da + " = load ptr, ptr " + dap);
            I(dbp + " = getelementptr inbounds %sylt.str, ptr " + sub.V + ", i32 0, i32 1");
            I(db + " = load ptr, ptr " + dbp);
            I(f + " = call ptr @strstr(ptr " + da + ", ptr " + db + ")");
            I(r + " = icmp ne ptr " + f + ", null");
            return {"i1", r, Type::Bool()};
        }
        throw Unsupported{c->Sp,
                          "el método `" + n + "` de strings no está soportado todavía por el "
                          "backend llvm"};
    }

    void GenBlockInner(const Block& b) {
        size_t mark = Undo.size();
        for (auto& s : b.Stmts) GenStmt(s.get());
        ScopeRestore(mark);
    }

    void GenStmt(Stmt* st) {
        switch (st->K) {
            case Stmt::Kind::Let: {
                auto* s = static_cast<LetStmt*>(st);
                if (s->Destructure)
                    throw Unsupported{s->Sp,
                                      "la destructuración de tuplas no está soportada "
                                      "todavía por el backend llvm"};
                RVal v = GenExpr(s->Init.get());
                if (s->Names[0] == "_") return;
                Type vt = s->VarType;
                std::string lt = LT(vt, s->Sp);
                v = CastVal(v, vt);
                std::string p = N();
                I(p + " = alloca " + lt);
                I("store " + lt + " " + v.V + ", ptr " + p);
                LetBind(s->Names[0], p);
                return;
            }
            case Stmt::Kind::Assign: {
                auto* s = static_cast<AssignStmt*>(st);
                Expr* t = s->Target.get();
                if (t->K == Expr::Kind::Index)
                    throw Unsupported{s->Sp,
                                      "asignar por índice no está soportado todavía por "
                                      "el backend llvm"};
                LVal lv = GenLval(t);
                RVal v = GenExpr(s->Value.get());
                if (s->Op == "=") {
                    std::string lt = LT(lv.Ty, s->Sp);
                    v = CastVal(v, lv.Ty);
                    I("store " + lt + " " + v.V + ", ptr " + lv.Ptr);
                    return;
                }
                std::string base = s->Op.substr(0, s->Op.size() - 1);
                std::string lt = LT(lv.Ty, s->Sp);
                std::string cur = N();
                I(cur + " = load " + lt + ", ptr " + lv.Ptr);
                if (lv.Ty.IsFloat() && v.Ty.IsInt()) v = CastVal(v, lv.Ty);
                std::string out = N();
                if (lv.Ty.IsStr() && base == "+") {
                    I(out + " = call ptr @sylt.str_concat(ptr " + cur + ", ptr " + v.V + ")");
                } else if (lv.Ty.IsInt()) {
                    std::string ins = (base == "+") ? "add i64" : (base == "-") ? "sub i64"
                                              : (base == "*") ? "mul i64"
                                              : (base == "/") ? "sdiv i64"
                                                              : "srem i64";
                    I(out + " = " + ins + " " + cur + ", " + v.V);
                } else {
                    std::string ins = (base == "+") ? "fadd double" : (base == "-") ? "fsub double"
                                                  : (base == "*") ? "fmul double"
                                                                  : "fdiv double";
                    I(out + " = " + ins + " " + cur + ", " + v.V);
                }
                I("store " + lt + " " + out + ", ptr " + lv.Ptr);
                return;
            }
            case Stmt::Kind::Expr:
                GenExpr(static_cast<ExprStmt*>(st)->Sub.get());
                return;
            case Stmt::Kind::Return: {
                auto* s = static_cast<ReturnStmt*>(st);
                if (!s->Value) {
                    Term("ret void");
                    return;
                }
                RVal v = GenExpr(s->Value.get());
                if (!RetType.IsError() && !RetType.IsVoid()) v = CastVal(v, RetType);
                Term("ret " + v.T + " " + v.V);
                return;
            }
            case Stmt::Kind::If: {
                auto* s = static_cast<IfStmt*>(st);
                std::string end = Lbl("ifend");
                RVal c = GenExpr(s->Cond.get());
                std::string thenL = Lbl("then"), nextL = Lbl("else");
                Term("br i1 " + c.V + ", label %" + thenL + ", label %" + nextL);
                Open(thenL);
                GenBlockInner(s->Then);
                if (!Closed.back()) Term("br label %" + end);
                for (auto& br : s->Elifs) {
                    Open(nextL);
                    RVal ec = GenExpr(br.Cond.get());
                    std::string eb = Lbl("then"), en = Lbl("else");
                    Term("br i1 " + ec.V + ", label %" + eb + ", label %" + en);
                    nextL = en;
                    Open(eb);
                    GenBlockInner(br.Body);
                    if (!Closed.back()) Term("br label %" + end);
                }
                if (s->HasElse) {
                    Open(nextL);
                    GenBlockInner(s->Else);
                    if (!Closed.back()) Term("br label %" + end);
                } else {
                    Open(nextL);
                    if (!Closed.back()) Term("br label %" + end);
                }
                Open(end);
                return;
            }
            case Stmt::Kind::While: {
                auto* s = static_cast<WhileStmt*>(st);
                std::string head = Lbl("while"), body = Lbl("wbody"), end = Lbl("wend");
                Term("br label %" + head);
                Open(head);
                RVal c = GenExpr(s->Cond.get());
                Term("br i1 " + c.V + ", label %" + body + ", label %" + end);
                Open(body);
                LoopStack.push_back({head, end});
                GenBlockInner(s->Body);
                LoopStack.pop_back();
                if (!Closed.back()) Term("br label %" + head);
                Open(end);
                return;
            }
            case Stmt::Kind::For: {
                auto* s = static_cast<ForStmt*>(st);
                if (!s->IsRange)
                    throw Unsupported{s->Sp,
                                      "iterar colecciones no está soportado todavía por el "
                                      "backend llvm; usa un rango `for I in 0..N`"};
                RVal lo = GenExpr(s->Low.get());
                RVal hi = GenExpr(s->High.get());
                std::string lp = N();
                I(lp + " = alloca i64");
                I("store i64 " + lo.V + ", ptr " + lp);
                std::string cond = Lbl("forcond"), body = Lbl("forbody");
                std::string step = Lbl("forstep"), end = Lbl("forend");
                Term("br label %" + cond);
                Open(cond);
                std::string i1 = N();
                I(i1 + " = load i64, ptr " + lp);
                std::string c = N();
                I(c + " = icmp slt i64 " + i1 + ", " + hi.V);
                Term("br i1 " + c + ", label %" + body + ", label %" + end);
                Open(body);
                size_t mark = Undo.size();
                LetBind(s->Var, lp);
                LoopStack.push_back({step, end});
                GenBlockInner(s->Body);
                LoopStack.pop_back();
                ScopeRestore(mark);
                if (!Closed.back()) Term("br label %" + step);
                Open(step);
                std::string i2 = N(), i3 = N();
                I(i2 + " = load i64, ptr " + lp);
                I(i3 + " = add i64 " + i2 + ", 1");
                I("store i64 " + i3 + ", ptr " + lp);
                Term("br label %" + cond);
                Open(end);
                return;
            }
            case Stmt::Kind::Break: {
                if (LoopStack.empty())
                    throw Unsupported{st->Sp, "break fuera de un bucle"};
                Term("br label %" + LoopStack.back().second);
                return;
            }
            case Stmt::Kind::Continue: {
                if (LoopStack.empty())
                    throw Unsupported{st->Sp, "continue fuera de un bucle"};
                Term("br label %" + LoopStack.back().first);
                return;
            }
            case Stmt::Kind::Match:
                GenMatch(static_cast<MatchStmt*>(st));
                return;
            case Stmt::Kind::Pass:
                return;
        }
    }

    void GenMatch(MatchStmt* s) {
        RVal sv = GenExpr(s->Subject.get());
        std::string sp = N();
        I(sp + " = alloca " + sv.T);
        I("store " + sv.T + " " + sv.V + ", ptr " + sp);
        std::string end = Lbl("matchend");
        std::string enumKey = s->Subject->Ty.Name;
        size_t n = s->Arms.size();
        for (size_t i = 0; i < n; i++) {
            auto& arm = s->Arms[i];
            std::string bodyL = Lbl("case"), nextL = Lbl("nextarm");
            bool last = (i + 1 == n);
            std::string fallthrough = last ? end : nextL;
            if (arm.Pat.k == Pattern::K::Wildcard) {
                Term("br label %" + bodyL);
            } else if (arm.Pat.k == Pattern::K::Literal) {
                std::string cur = N();
                I(cur + " = load " + sv.T + ", ptr " + sp);
                RVal pv = GenExpr(arm.Pat.Lit.get());
                if (s->Subject->Ty.IsFloat() && pv.Ty.IsInt()) pv = CastVal(pv, s->Subject->Ty);
                std::string cond;
                if (s->Subject->Ty.IsStr()) {
                    std::string cc = N();
                    I(cc + " = call i32 @sylt.str_cmp(ptr " + cur + ", ptr " + pv.V + ")");
                    std::string z = N();
                    I(z + " = icmp eq i32 " + cc + ", 0");
                    cond = z;
                } else if (s->Subject->Ty.IsFloat()) {
                    std::string z = N();
                    I(z + " = fcmp oeq double " + cur + ", " + pv.V);
                    cond = z;
                } else if (s->Subject->Ty.IsBool()) {
                    std::string z = N();
                    I(z + " = icmp eq i1 " + cur + ", " + pv.V);
                    cond = z;
                } else {
                    std::string z = N();
                    I(z + " = icmp eq i64 " + cur + ", " + pv.V);
                    cond = z;
                }
                Term("br i1 " + cond + ", label %" + bodyL + ", label %" + fallthrough);
            } else {
                std::string tagp = N(), tag = N(), cond = N();
                I(tagp + " = getelementptr inbounds %" + enumKey + ", ptr " + sp +
                  ", i32 0, i32 0");
                I(tag + " = load i64, ptr " + tagp);
                I(cond + " = icmp eq i64 " + tag + ", " + std::to_string(arm.Pat.TagIndex));
                Term("br i1 " + cond + ", label %" + bodyL + ", label %" + fallthrough);
            }
            Open(bodyL);
            size_t mark = Undo.size();
            if (arm.Pat.k == Pattern::K::Variant) {
                for (size_t j = 0; j < arm.Pat.Bindings.size(); j++) {
                    if (arm.Pat.Bindings[j] == "_") continue;
                    int pos = 0;
                    auto ek = EnumFieldPos.find(enumKey);
                    if (ek != EnumFieldPos.end()) {
                        auto fp = ek->second.find(arm.Pat.FieldNames[j]);
                        if (fp != ek->second.end()) pos = fp->second;
                    }
                    std::string bp = N();
                    I(bp + " = getelementptr inbounds %" + enumKey + ", ptr " + sp +
                      ", i32 0, i32 " + std::to_string(1 + pos));
                    LetBind(arm.Pat.Bindings[j], bp);
                }
            }
            GenBlockInner(arm.Body);
            ScopeRestore(mark);
            if (!Closed.back()) Term("br label %" + end);
            if (!last) Open(nextL);
        }
        Open(end);
    }

    void Run() {
        for (auto& im : Prog->Imports)
            if (im.Module == "std" || StartsWith(im.Module, "std."))
                throw Unsupported{im.Sp,
                                  "la librería std.* usa el runtime de C++ y no está "
                                  "disponible en el backend llvm (llega en v0.4)"};
        EmitTypes();
        EmitDeclares();
        EmitStrOfProtos();
        for (auto& fd : Prog->Fns)
            if (!fd->Foreign) EmitFnDecl(*fd);
        for (auto& td : Prog->Types)
            for (auto& m : td->Methods)
                EmitFnDecl(*m, Key(td->Module, td->Name) + "__" + m->Name);
        EmitMain();
    }

    void EmitTypes() {
        for (auto& td : Prog->Types) {
            std::string key = Key(td->Module, td->Name);
            CurModule = td->Module;
            std::string s = "%" + key + " = type { ";
            for (size_t i = 0; i < td->Fields.size(); i++) {
                if (i) s += ", ";
                s += LT(TypeOf(td->Fields[i].Ty), td->Fields[i].Sp);
            }
            CurModule = "";
            Ts << s + " }\n";
        }
        for (auto& ed : Prog->Enums) {
            std::string key = Key(ed->Module, ed->Name);
            std::vector<std::string> order;
            std::unordered_map<std::string, Type> kinds;
            CurModule = ed->Module;
            for (auto& v : ed->Variants)
                for (auto& f : v.Fields) {
                    if (kinds.count(f.Name)) continue;
                    kinds[f.Name] = TypeOf(f.Ty);
                    order.push_back(f.Name);
                }
            CurModule = "";
            std::string s = "%" + key + " = type { i64";
            for (size_t i = 0; i < order.size(); i++) {
                s += ", " + LT(kinds[order[i]], ed->Sp);
                EnumFieldPos[key][order[i]] = (int)i;
            }
            Ts << s + " }\n";
        }
    }

    void EmitDeclares() {
        Ds << "@stderr = external global ptr\n";
        Ds << "declare ptr @malloc(i64)\n";
        Ds << "declare i64 @strlen(ptr)\n";
        Ds << "declare ptr @memcpy(ptr, ptr, i64)\n";
        Ds << "declare i32 @snprintf(ptr, i64, ptr, ...)\n";
        Ds << "declare i32 @puts(ptr)\n";
        Ds << "declare i32 @fprintf(ptr, ptr, ...)\n";
        Ds << "declare void @exit(i32)\n";
        Ds << "declare i32 @strcmp(ptr, ptr)\n";
        Ds << "declare ptr @strchr(ptr, i32)\n";
        Ds << "declare ptr @strstr(ptr, ptr)\n";
        Ds << "declare double @llvm.sqrt.f64(double)\n";
        Ds << "declare double @llvm.pow.f64(double, double)\n";
        Ds << "declare double @llvm.floor.f64(double)\n";
        for (auto& fd : Prog->Fns)
            if (fd->Foreign) EmitForeignDecl(*fd);
        Ds << "\n";
    }

    std::string ParamLT(Param& p) {
        if (p.IsSelf) return "ptr";
        Type t = TypeOf(p.Ty);
        if (p.Ty.k == TypeAnn::K::Borrow) {
            Type pt = t.Pointee();
            if (!pt.IsStruct() && !pt.IsEnum())
                throw Unsupported{p.Sp,
                                  "los préstamos de tipos básicos no están soportados "
                                  "todavía por el backend llvm"};
            return "ptr";
        }
        if (t.IsStruct() || t.IsEnum()) return "ptr";
        return LT(t, p.Sp);
    }

    std::string SigOf(FnDecl& fd, const std::string& nameOverride) {
        CurModule = fd.Module;
        std::string ret = fd.HasRet ? LT(TypeOf(fd.Ret), fd.Sp) : "void";
        std::string sig = "define internal " + ret + " @" +
                          (nameOverride.empty() ? Key(fd.Module, fd.Name) : nameOverride) + "(";
        for (size_t i = 0; i < fd.Params.size(); i++) {
            if (i) sig += ", ";
            sig += ParamLT(fd.Params[i]) + " %a" + std::to_string(i);
        }
        CurModule = "";
        return sig + ")";
    }

    void EmitForeignDecl(FnDecl& fd) {
        CurModule = fd.Module;
        std::string ret = fd.HasRet ? LT(TypeOf(fd.Ret), fd.Sp) : "void";
        std::string sig = "declare " + ret + " @" + fd.Name + "(";
        for (size_t i = 0; i < fd.Params.size(); i++) {
            if (i) sig += ", ";
            Type t = TypeOf(fd.Params[i].Ty);
            std::string lt = LT(t, fd.Params[i].Sp);
            if (t.IsStr()) sig += "ptr";
            else sig += lt;
        }
        CurModule = "";
        Ds << sig << ")\n";
    }

    void EmitFnDecl(FnDecl& fd, const std::string& nameOverride = "") {
        std::string sig = SigOf(fd, nameOverride);
        CurModule = fd.Module;
        RetType = fd.HasRet ? TypeOf(fd.Ret) : Type::Void();
        Blocks.clear();
        Closed.clear();
        LoopStack.clear();
        Undo.clear();
        VarPtr.clear();
        Open("");
        for (size_t i = 0; i < fd.Params.size(); i++) {
            Param& p = fd.Params[i];
            std::string preg = "%a" + std::to_string(i);
            std::string pname = p.IsSelf ? "self" : p.Name;
            Type t = TypeOf(p.Ty);
            bool borrow = p.Ty.k == TypeAnn::K::Borrow;
            bool agg = t.IsStruct() || t.IsEnum();
            if (p.IsSelf || borrow) {
                VarPtr[pname] = preg;
            } else if (agg) {
                std::string lt = LT(t, p.Sp);
                std::string v = N();
                I(v + " = load " + lt + ", ptr " + preg);
                std::string lp = N();
                I(lp + " = alloca " + lt);
                I("store " + lt + " " + v + ", ptr " + lp);
                VarPtr[pname] = lp;
            } else {
                std::string lt = LT(t, p.Sp);
                std::string lp = N();
                I(lp + " = alloca " + lt);
                I("store " + lt + " " + preg + ", ptr " + lp);
                VarPtr[pname] = lp;
            }
        }
        GenBlockInner(fd.Body);
        if (Closed.empty() || !Closed.back()) {
            std::string lt = RetType.K == TK::Void ? "" : LT(RetType, fd.Sp);
            if (RetType.K == TK::Void) Term("ret void");
            else if (RetType.IsInt()) Term("ret i64 0");
            else if (RetType.IsFloat()) Term("ret double 0.0");
            else if (RetType.IsBool()) Term("ret i1 false");
            else if (RetType.IsStr()) Term("ret ptr null");
            else Term("ret " + lt + " zeroinitializer");
        }
        std::string text;
        for (size_t i = 0; i < Blocks.size(); i++) {
            if (!Blocks[i].first.empty()) text += Blocks[i].first + ":\n";
            text += Blocks[i].second;
            if (!Closed[i]) text += "  unreachable\n";
        }
        Bs << sig << " {\n" << text << "}\n\n";
        RetType = Type::Err();
        CurModule = "";
    }

    void EmitMain() {
        auto f = FnByName.find("Main");
        if (f == FnByName.end()) return;
        Bs << "define i32 @main() {\n"
              "  call void @Main()\n"
              "  ret i32 0\n"
              "}\n\n";
    }

    void EmitStrOfProtos() {
        for (auto& td : Prog->Types) EmitStrOfStruct(*td);
        for (auto& ed : Prog->Enums) EmitStrOfEnum(*ed);
    }

    std::string StrOfField(const std::string& aggKey, const std::string& vreg, FieldDecl& f,
                           int idx) {
        Type ft = TypeOf(f.Ty);
        std::string lt = LT(ft, f.Sp);
        std::string fp = N();
        I(fp + " = getelementptr inbounds %" + aggKey + ", ptr " + vreg + ", i32 0, i32 " +
          std::to_string(idx));
        if (ft.IsStr()) return fp;
        if (ft.IsStruct() || ft.IsEnum()) {
            std::string r = N();
            I(r + " = call ptr @sylt.str_of__" + ft.Name + "(ptr " + fp + ")");
            return r;
        }
        std::string v = N();
        I(v + " = load " + lt + ", ptr " + fp);
        return ToStrVal(v, ft);
    }

    void EmitStrOfStruct(TypeDecl& td) {
        std::string key = Key(td.Module, td.Name);
        Blocks.clear();
        Closed.clear();
        Undo.clear();
        VarPtr.clear();
        Open("");
        std::string acc = StrLit(td.Name + "{");
        for (size_t i = 0; i < td.Fields.size(); i++) {
            std::string sep = StrLit(std::string(i == 0 ? "" : ", ") + td.Fields[i].Name + ": ");
            std::string t1 = N();
            I(t1 + " = call ptr @sylt.str_concat(ptr " + acc + ", ptr " + sep + ")");
            CurModule = td.Module;
            std::string fs = StrOfField(key, "%a0", td.Fields[i], (int)i);
            CurModule = "";
            std::string t2 = N();
            I(t2 + " = call ptr @sylt.str_concat(ptr " + t1 + ", ptr " + fs + ")");
            acc = t2;
        }
        std::string endp = StrLit("}");
        std::string t3 = N();
        I(t3 + " = call ptr @sylt.str_concat(ptr " + acc + ", ptr " + endp + ")");
        Term("ret ptr " + t3);
        std::string text;
        for (size_t i = 0; i < Blocks.size(); i++) {
            if (!Blocks[i].first.empty()) text += Blocks[i].first + ":\n";
            text += Blocks[i].second;
            if (!Closed[i]) text += "  unreachable\n";
        }
        Bs << "define private ptr @sylt.str_of__" << key << "(ptr %a0) {\n"
           << text << "}\n\n";
    }

    void EmitStrOfEnum(EnumDecl& ed) {
        std::string key = Key(ed.Module, ed.Name);
        Blocks.clear();
        Closed.clear();
        Undo.clear();
        VarPtr.clear();
        Open("");
        std::string tagp = N();
        I(tagp + " = getelementptr inbounds %" + key + ", ptr %a0, i32 0, i32 0");
        std::string tag = N();
        I(tag + " = load i64, ptr " + tagp);
        std::string rp = N();
        I(rp + " = alloca ptr");
        std::string end = Lbl("strend");
        for (size_t vi = 0; vi < ed.Variants.size(); vi++) {
            EnumVariant& v = ed.Variants[vi];
            std::string bodyL = Lbl("var"), nextL = Lbl("nxt");
            std::string c = N();
            I(c + " = icmp eq i64 " + tag + ", " + std::to_string(vi));
            Term("br i1 " + c + ", label %" + bodyL + ", label %" + nextL);
            Open(bodyL);
            std::string acc;
            if (v.Fields.empty()) {
                acc = StrLit(v.Name);
            } else {
                acc = StrLit(v.Name + "{");
                for (size_t i = 0; i < v.Fields.size(); i++) {
                    std::string sep =
                        StrLit(std::string(i == 0 ? "" : ", ") + v.Fields[i].Name + ": ");
                    std::string t1 = N();
                    I(t1 + " = call ptr @sylt.str_concat(ptr " + acc + ", ptr " + sep + ")");
                    CurModule = ed.Module;
                    int pos = EnumFieldPos[key][v.Fields[i].Name];
                    std::string fs = StrOfField(key, "%a0", v.Fields[i], pos + 1);
                    CurModule = "";
                    std::string t2 = N();
                    I(t2 + " = call ptr @sylt.str_concat(ptr " + t1 + ", ptr " + fs + ")");
                    acc = t2;
                }
                std::string endp = StrLit("}");
                std::string t3 = N();
                I(t3 + " = call ptr @sylt.str_concat(ptr " + acc + ", ptr " + endp + ")");
                acc = t3;
            }
            I("store ptr " + acc + ", ptr " + rp);
            Term("br label %" + end);
            Open(nextL);
        }
        std::string unk = StrLit(ed.Name + "{?}");
        I("store ptr " + unk + ", ptr " + rp);
        Term("br label %" + end);
        Open(end);
        std::string r = N();
        I(r + " = load ptr, ptr " + rp);
        Term("ret ptr " + r);
        std::string text;
        for (size_t i = 0; i < Blocks.size(); i++) {
            if (!Blocks[i].first.empty()) text += Blocks[i].first + ":\n";
            text += Blocks[i].second;
            if (!Closed[i]) text += "  unreachable\n";
        }
        (void)unk;
        Bs << "define private ptr @sylt.str_of__" << key << "(ptr %a0) {\n"
           << text << "}\n\n";
    }

    std::string EmitRuntime() {
        return R"RT(%sylt.str = type { i64, ptr }

define private ptr @sylt.str_lit(ptr %c) {
  %l = call i64 @strlen(ptr %c)
  %tot = add i64 %l, 25
  %m = call ptr @malloc(i64 %tot)
  %hp = getelementptr %sylt.str, ptr %m, i32 0, i32 0
  store i64 %l, ptr %hp
  %d = getelementptr i8, ptr %m, i64 16
  %x1 = call ptr @memcpy(ptr %d, ptr %c, i64 %l)
  %n = getelementptr i8, ptr %d, i64 %l
  store i8 0, ptr %n
  %dp = getelementptr %sylt.str, ptr %m, i32 0, i32 1
  store ptr %d, ptr %dp
  ret ptr %m
}

define private ptr @sylt.str_concat(ptr %a, ptr %b) {
  %hap = getelementptr %sylt.str, ptr %a, i32 0, i32 0
  %la = load i64, ptr %hap
  %hbp = getelementptr %sylt.str, ptr %b, i32 0, i32 0
  %lb = load i64, ptr %hbp
  %lt = add i64 %la, %lb
  %tot = add i64 %lt, 25
  %m = call ptr @malloc(i64 %tot)
  %hp = getelementptr %sylt.str, ptr %m, i32 0, i32 0
  store i64 %lt, ptr %hp
  %d = getelementptr i8, ptr %m, i64 16
  %dap = getelementptr %sylt.str, ptr %a, i32 0, i32 1
  %da = load ptr, ptr %dap
  %x1 = call ptr @memcpy(ptr %d, ptr %da, i64 %la)
  %db = getelementptr i8, ptr %d, i64 %la
  %dbp = getelementptr %sylt.str, ptr %b, i32 0, i32 1
  %dvb = load ptr, ptr %dbp
  %x2 = call ptr @memcpy(ptr %db, ptr %dvb, i64 %lb)
  %n = getelementptr i8, ptr %d, i64 %lt
  store i8 0, ptr %n
  %dp = getelementptr %sylt.str, ptr %m, i32 0, i32 1
  store ptr %d, ptr %dp
  ret ptr %m
}

define private i32 @sylt.str_cmp(ptr %a, ptr %b) {
  %dap = getelementptr %sylt.str, ptr %a, i32 0, i32 1
  %da = load ptr, ptr %dap
  %dbp = getelementptr %sylt.str, ptr %b, i32 0, i32 1
  %db = load ptr, ptr %dbp
  %r = call i32 @strcmp(ptr %da, ptr %db)
  ret i32 %r
}

define private ptr @sylt.i64_str(i64 %v) {
  %b = alloca [32 x i8]
  %n = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %b, i64 32, ptr @.fmt.i64, i64 %v)
  %r = call ptr @sylt.str_lit(ptr %b)
  ret ptr %r
}

define private ptr @sylt.f64_str(double %v) {
  %b = alloca [64 x i8]
  %n1 = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %b, i64 64, ptr @.fmt.g, double %v)
  %d1 = call ptr @strchr(ptr %b, i32 46)
  %h1 = icmp ne ptr %d1, null
  %d2 = call ptr @strchr(ptr %b, i32 101)
  %h2 = icmp ne ptr %d2, null
  %d3 = call ptr @strchr(ptr %b, i32 110)
  %h3 = icmp ne ptr %d3, null
  %d4 = call ptr @strchr(ptr %b, i32 105)
  %h4 = icmp ne ptr %d4, null
  %a12 = or i1 %h1, %h2
  %a34 = or i1 %h3, %h4
  %any = or i1 %a12, %a34
  %plain = xor i1 %any, true
  br i1 %plain, label %plainb, label %done
plainb:
  %n2 = call i32 (ptr, i64, ptr, ...) @snprintf(ptr %b, i64 64, ptr @.fmt.g0, double %v)
  br label %done
done:
  %r = call ptr @sylt.str_lit(ptr %b)
  ret ptr %r
}

define private ptr @sylt.bool_str(i1 %v) {
  %p = select i1 %v, ptr @.s.true, ptr @.s.false
  %r = call ptr @sylt.str_lit(ptr %p)
  ret ptr %r
}

define private void @sylt.print(ptr %s) {
  %dp = getelementptr %sylt.str, ptr %s, i32 0, i32 1
  %d = load ptr, ptr %dp
  %r = call i32 @puts(ptr %d)
  ret void
}

define private void @sylt.rt_div0() {
  %se = load ptr, ptr @stderr
  %r = call i32 (ptr, ptr, ...) @fprintf(ptr %se, ptr @.fmt.s, ptr MSGDIV)
  call void @exit(i32 1)
  unreachable
}

define private void @sylt.rt_mod0() {
  %se = load ptr, ptr @stderr
  %r = call i32 (ptr, ptr, ...) @fprintf(ptr %se, ptr @.fmt.s, ptr MSGMOD)
  call void @exit(i32 1)
  unreachable
}
)RT";
    }
};

}

std::string GenLlvm(ProgramP& prog, const GenOptions& opt) {
    G g(prog, opt);
    std::string msgDiv = g.StrConst("error en ejecución: división entera por cero\n");
    std::string msgMod = g.StrConst("error en ejecución: módulo entero por cero\n");
    g.Run();
    std::string rt = g.EmitRuntime();
    size_t p1 = rt.find("MSGDIV");
    if (p1 != std::string::npos) rt.replace(p1, 6, msgDiv);
    size_t p2 = rt.find("MSGMOD");
    if (p2 != std::string::npos) rt.replace(p2, 6, msgMod);
    std::ostringstream Out;
    Out << g.Ts.str() << "\n" << g.Gs.str() << "\n" << g.Ds.str() << "\n";
    Out << "@.fmt.i64 = private unnamed_addr constant [5 x i8] c\"%lld\\00\"\n";
    Out << "@.fmt.g = private unnamed_addr constant [3 x i8] c\"%g\\00\"\n";
    Out << "@.fmt.g0 = private unnamed_addr constant [5 x i8] c\"%g.0\\00\"\n";
    Out << "@.fmt.s = private unnamed_addr constant [3 x i8] c\"%s\\00\"\n";
    Out << "@.s.true = private unnamed_addr constant [5 x i8] c\"true\\00\"\n";
    Out << "@.s.false = private unnamed_addr constant [6 x i8] c\"false\\00\"\n\n";
    Out << rt << "\n" << g.Bs.str();
    return Out.str();
}

}
