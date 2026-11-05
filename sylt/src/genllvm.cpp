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
    std::unordered_map<std::string, FnDecl*> ForeignDecls;
    std::unordered_map<std::string, std::string> QualNames;
    std::unordered_map<std::string, std::string> VarPtr;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> EnumFieldPos;
    std::vector<std::pair<std::string, std::string>> Undo;
    std::vector<std::pair<std::string, std::string>> LoopStack;
    Type RetType;
    std::string CurModule;
    std::unordered_map<std::string, std::string> TupleNames;
    std::unordered_map<std::string, std::string> CollStr;
    std::vector<std::pair<Type, int>> PendColl;
    int TupleN = 0;
    std::vector<std::pair<std::string, std::string>> Allos;

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
        for (auto& fd : Prog->Fns) {
            FnByName[Key(fd->Module, fd->Name)] = fd.get();
            if (fd->Foreign) ForeignDecls[fd->Name] = fd.get();
        }
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
        if (a.HasResolvedTy) return a.ResolvedTy;
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
                if (a.Name.find('[') != std::string::npos) {
                    if (TypeByName.count(a.Name) || EnumByName.count(a.Name))
                        return Type::StructOf(a.Name);
                    return Type::Err();
                }
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
            case TypeAnn::K::Generic: return Type::Err();
        }
        return Type::Err();
    }

    static std::string Q(const std::string& id) { return "\"" + id + "\""; }

    std::string LT(const Type& t, const Span& sp) {
        switch (t.K) {
            case TK::Int: return "i64";
            case TK::Float: return "double";
            case TK::Bool: return "i1";
            case TK::Str: return "ptr";
            case TK::Void: return "void";
            case TK::Struct: return "%" + Q(CppId(Canoniza(t.Name)));
            case TK::Enum: return "%" + Q(CppId(Canoniza(t.Name)));
            case TK::Borrow: return LT(t.Pointee(), sp);
            case TK::Array: return "ptr";
            case TK::Map: return "ptr";
            case TK::Tuple: return "%" + TupleType(t, sp);
            default:
                throw Unsupported{sp, "este tipo no está soportado por el backend llvm"};
        }
    }

    std::string Canoniza(const std::string& n) {
        if (n.find("__") != std::string::npos) return n;
        if (!CurModule.empty()) {
            std::string m = CurModule;
            for (auto& c : m)
                if (c == '.') c = '_';
            std::string k = m + "__" + n;
            if (TypeByName.count(k) || EnumByName.count(k)) return k;
        }
        return n;
    }

    std::string ShapeKey(const Type& t) {
        switch (t.K) {
            case TK::Int: return "i64";
            case TK::Float: return "f64";
            case TK::Bool: return "i1";
            case TK::Str: return "str";
            case TK::Array: return "a" + ShapeKey(t.Sub[0]);
            case TK::Map: return "m" + ShapeKey(t.Sub[0]) + ShapeKey(t.Sub[1]);
            case TK::Tuple: {
                std::string s = "t";
                for (auto& e : t.Sub) s += ShapeKey(e);
                return s;
            }
            default: return CppId(Canoniza(t.Name));
        }
    }

    std::string TupleType(const Type& t, const Span& sp) {
        std::string key = ShapeKey(t);
        auto it = TupleNames.find(key);
        if (it != TupleNames.end()) return it->second;
        std::string name = "sylt.t." + std::to_string(TupleN++);
        TupleNames[key] = name;
        std::string s = "%" + name + " = type { ";
        for (size_t i = 0; i < t.Sub.size(); i++) {
            if (i) s += ", ";
            s += LT(t.Sub[i], sp);
        }
        Ts << s + " }\n";
        return name;
    }

    std::string TypeSize(const Type& t, const Span& sp) {
        if (t.IsInt() || t.IsFloat()) return "8";
        if (t.IsBool()) return "1";
        if (t.IsStr() || t.IsArray() || t.IsMap()) return "8";
        std::string lt = LT(t, sp);
        std::string g = N(), r = N();
        I(g + " = getelementptr " + lt + ", ptr null, i64 1");
        I(r + " = ptrtoint ptr " + g + " to i64");
        return r;
    }

    std::string AllocaT(const std::string& lt) {
        std::string r = N();
        Allos.push_back({lt, r});
        return r;
    }

    std::string Junta() {
        std::string allo;
        for (auto& a : Allos) allo += "  " + a.second + " = alloca " + a.first + "\n";
        Allos.clear();
        std::string text;
        for (size_t i = 0; i < Blocks.size(); i++) {
            if (!Blocks[i].first.empty()) text += Blocks[i].first + ":\n";
            if (i == 0 && !allo.empty()) text += allo;
            text += Blocks[i].second;
            if (!Closed[i]) text += "  unreachable\n";
        }
        return text;
    }

    std::string BaseName(Expr* e) {
        Expr* r = e;
        while (r->K == Expr::Kind::Member || r->K == Expr::Kind::Index) {
            r = (r->K == Expr::Kind::Member)
                    ? static_cast<MemberExpr*>(r)->Base.get()
                    : static_cast<IndexExpr*>(r)->Base.get();
        }
        if (r->K == Expr::Kind::Ident) return static_cast<IdentExpr*>(r)->Name;
        return "expresión";
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
            std::string tid = CppId(t.Name);
            std::string tmp = AllocaT("%" + Q(tid));
            I("store %" + Q(tid) + " " + reg + ", ptr " + tmp);
            I(r + " = call ptr @" + Q("sylt.str_of__" + tid) + "(ptr " + tmp + ")");
        } else if (t.IsArray()) {
            std::string fn = CollStrFn(t, 0);
            I(r + " = call ptr @" + fn + "(ptr " + reg + ")");
        } else if (t.IsMap()) {
            std::string fn = CollStrFn(t, 1);
            I(r + " = call ptr @" + fn + "(ptr " + reg + ")");
        } else throw Unsupported{Span(), "no puedo convertir este valor a texto en el backend llvm"};
        return r;
    }

    std::string CollStrFn(const Type& t, int kind) {
        std::string key = (kind == 0 ? "a:" : "m:") + ShapeKey(t);
        auto it = CollStr.find(key);
        if (it != CollStr.end()) return it->second;
        std::string fn = "sylt.coll_str." + std::to_string(CollStr.size());
        CollStr[key] = fn;
        PendColl.push_back({t, kind});
        return fn;
    }

    std::string DatoATexto(const std::string& slot, const Type& t, const Span& sp) {
        if (t.IsStr()) {
            std::string h = N();
            I(h + " = load ptr, ptr " + slot);
            return h;
        }
        if (t.IsArray() || t.IsMap()) return ToStrVal(slot, t);
        if (t.IsStruct() || t.IsEnum()) {
            std::string s = N();
            I(s + " = load " + LT(t, sp) + ", ptr " + slot);
            return ToStrVal(s, t);
        }
        std::string v = N();
        I(v + " = load " + LT(t, sp) + ", ptr " + slot);
        return ToStrVal(v, t);
    }

    void EmitCollStr(const Type& t, int kind) {
        std::string fn = CollStrFn(t, kind);
        Blocks.clear();
        Closed.clear();
        LoopStack.clear();
        Undo.clear();
        VarPtr.clear();
        Open("");
        std::string acc = AllocaT("ptr");
        std::string ini = StrLit(kind == 0 ? "[" : "{");
        I("store ptr " + ini + ", ptr " + acc);
        std::string len = N();
        I(len + " = call i64 @" + (kind == 0 ? "sy_arr_len" : "sy_map_len") + "(ptr %a0)");
        std::string iv = AllocaT("i64");
        I("store i64 0, ptr " + iv);
        std::string head = Lbl("cs"), body = Lbl("csbody"), step = Lbl("csstep"), end = Lbl("csend");
        Term("br label %" + head);
        Open(head);
        std::string v1 = N(), c1 = N();
        I(v1 + " = load i64, ptr " + iv);
        I(c1 + " = icmp slt i64 " + v1 + ", " + len);
        Term("br i1 " + c1 + ", label %" + body + ", label %" + end);
        Open(body);
        std::string v2 = N();
        I(v2 + " = load i64, ptr " + iv);
        std::string sep;
        if (kind == 0) {
            Type et = t.Sub[0];
            std::string sz = TypeSize(et, Span());
            std::string slot = N();
            I(slot + " = call ptr @sy_arr_slot(ptr %a0, i64 " + v2 + ", i64 " + sz +
              ", ptr " + StrConst("colección") + ")");
            std::string txt = DatoATexto(slot, et, Span());
            std::string c2 = N(), se = N();
            I(c2 + " = icmp eq i64 " + v2 + ", 0");
            I(se + " = select i1 " + c2 + ", ptr " + StrLit("") + ", ptr " + StrLit(", "));
            std::string t1 = N();
            I(t1 + " = call ptr @sylt.str_concat(ptr " + sepload(acc) + ", ptr " + se + ")");
            std::string t2 = N();
            I(t2 + " = call ptr @sylt.str_concat(ptr " + t1 + ", ptr " + txt + ")");
            I("store ptr " + t2 + ", ptr " + acc);
        } else {
            Type kt = t.Sub[0], vt = t.Sub[1];
            std::string vsz = TypeSize(vt, Span());
            std::string ktxt;
            if (kt.IsStr()) {
                std::string kh = N();
                I(kh + " = call ptr @sy_map_clave_str(ptr %a0, i64 " + v2 + ")");
                ktxt = kh;
            } else {
                std::string kv = N();
                I(kv + " = call i64 @sy_map_clave_int(ptr %a0, i64 " + v2 + ")");
                ktxt = ToStrVal(kv, kt);
            }
            std::string vp = N();
            I(vp + " = call ptr @sy_map_valor(ptr %a0, i64 " + v2 + ", i64 " + vsz + ")");
            std::string vtxt = DatoATexto(vp, vt, Span());
            std::string c2 = N(), se = N();
            I(c2 + " = icmp eq i64 " + v2 + ", 0");
            I(se + " = select i1 " + c2 + ", ptr " + StrLit("") + ", ptr " + StrLit(", "));
            std::string t1 = N();
            I(t1 + " = call ptr @sylt.str_concat(ptr " + sepload(acc) + ", ptr " + se + ")");
            std::string t2 = N();
            I(t2 + " = call ptr @sylt.str_concat(ptr " + t1 + ", ptr " + ktxt + ")");
            std::string t3 = N();
            I(t3 + " = call ptr @sylt.str_concat(ptr " + t2 + ", ptr " + StrLit(": ") + ")");
            std::string t4 = N();
            I(t4 + " = call ptr @sylt.str_concat(ptr " + t3 + ", ptr " + vtxt + ")");
            I("store ptr " + t4 + ", ptr " + acc);
        }
        Term("br label %" + step);
        Open(step);
        std::string v3 = N(), v4 = N();
        I(v3 + " = load i64, ptr " + iv);
        I(v4 + " = add i64 " + v3 + ", 1");
        I("store i64 " + v4 + ", ptr " + iv);
        Term("br label %" + head);
        Open(end);
        std::string fin = N();
        I(fin + " = call ptr @sylt.str_concat(ptr " + sepload(acc) + ", ptr " +
          StrLit(kind == 0 ? "]" : "}") + ")");
        Term("ret ptr " + fin);
        Bs << "define private ptr @" << fn << "(ptr %a0) {\n" << Junta() << "}\n\n";
    }

    std::string sepload(const std::string& acc) {
        std::string r = N();
        I(r + " = load ptr, ptr " + acc);
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
            auto f = TypeByName.find(Canoniza(bt.Name));
            if (f == TypeByName.end())
                throw Unsupported{m->Sp, "tipo desconocido en el backend llvm"};
            int idx = FieldIndex(f->second, m->Name);
            std::string r = N();
            I(r + " = getelementptr inbounds %" + Q(CppId(Canoniza(bt.Name))) + ", ptr " + b.Ptr +
              ", i32 0, i32 " + std::to_string(idx));
            return {r, m->Ty};
        }
        if (e->K == Expr::Kind::Index) {
            auto* ix = static_cast<IndexExpr*>(e);
            if (ix->TupleIndex) {
                LVal b = GenLval(ix->Base.get());
                std::string tn = TupleType(ix->Base->Ty, ix->Sp);
                std::string r = N();
                I(r + " = getelementptr inbounds %" + tn + ", ptr " + b.Ptr +
                  ", i32 0, i32 " + std::to_string(ix->TuplePos));
                return {r, e->Ty};
            }
            Type bt = ix->Base->Ty.Pointee();
            RVal b = GenExpr(ix->Base.get());
            RVal iv = GenExpr(ix->Idx.get());
            std::string nom = StrConst(BaseName(ix->Base.get()));
            if (bt.IsArray()) {
                Type et = bt.Sub[0];
                std::string sz = TypeSize(et, ix->Sp);
                std::string slot = N();
                I(slot + " = call ptr @sy_arr_slot(ptr " + b.V + ", i64 " + iv.V + ", i64 " +
                  sz + ", ptr " + nom + ")");
                return {slot, et};
            }
            if (bt.IsMap()) {
                Type kt = bt.Sub[0], vt = bt.Sub[1];
                std::string vsz = TypeSize(vt, ix->Sp);
                std::string slot = N();
                if (kt.IsStr())
                    I(slot + " = call ptr @sy_map_slot_ins_str(ptr " + b.V + ", ptr " + iv.V +
                      ", i64 " + vsz + ")");
                else
                    I(slot + " = call ptr @sy_map_slot_ins_int(ptr " + b.V + ", i64 " + iv.V +
                      ", i64 " + vsz + ")");
                return {slot, vt};
            }
            throw Unsupported{ix->Sp, "no puedo asignar a este índice en el backend llvm"};
        }
        if (e->K == Expr::Kind::Call) {
            RVal v = GenExpr(e);
            std::string t = AllocaT(v.T);
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
                return GenIndex(e);
            case Expr::Kind::ArrayLit: {
                auto* a = static_cast<ArrayLitExpr*>(e);
                Type et = a->Ty.Sub[0];
                std::string sz = TypeSize(et, a->Sp);
                std::string arr = N();
                I(arr + " = call ptr @sy_arr_nuevo(i64 " + sz + ")");
                for (size_t i = 0; i < a->Elems.size(); i++) {
                    RVal v = GenExpr(a->Elems[i].get());
                    v = CastVal(v, et);
                    std::string tmp = AllocaT(LT(et, a->Sp));
                    I("store " + v.T + " " + v.V + ", ptr " + tmp);
                    I("call ptr @sy_arr_push(ptr " + arr + ", ptr " + tmp + ", i64 " + sz + ")");
                }
                return {"ptr", arr, a->Ty};
            }
            case Expr::Kind::MapLit: {
                auto* m = static_cast<MapLitExpr*>(e);
                Type kt = m->Ty.Sub[0], vt = m->Ty.Sub[1];
                int kkind;
                if (kt.IsStr()) kkind = 0;
                else if (kt.IsInt()) kkind = 1;
                else
                    throw Unsupported{m->Sp,
                                      "solo hay mapas con claves `string` o `int` en el "
                                      "backend llvm"};
                std::string vsz = TypeSize(vt, m->Sp);
                std::string mp = N();
                I(mp + " = call ptr @sy_map_nuevo(i64 " + std::to_string(kkind) + ", i64 " +
                  vsz + ")");
                for (auto& pr : m->Pairs) {
                    RVal v = GenExpr(pr.Val.get());
                    v = CastVal(v, vt);
                    std::string tmp = AllocaT(LT(vt, m->Sp));
                    I("store " + v.T + " " + v.V + ", ptr " + tmp);
                    if (kkind == 0) {
                        RVal k = GenExpr(pr.Key.get());
                        I("call ptr @sy_map_pon_str(ptr " + mp + ", ptr " + k.V + ", ptr " +
                          tmp + ", i64 " + vsz + ")");
                    } else {
                        RVal k = GenExpr(pr.Key.get());
                        I("call ptr @sy_map_pon_int(ptr " + mp + ", i64 " + k.V + ", ptr " +
                          tmp + ", i64 " + vsz + ")");
                    }
                }
                return {"ptr", mp, m->Ty};
            }
            case Expr::Kind::TupleLit: {
                auto* t = static_cast<TupleLitExpr*>(e);
                std::string tn = TupleType(t->Ty, t->Sp);
                std::string tmp = AllocaT("%" + tn);
                for (size_t i = 0; i < t->Elems.size(); i++) {
                    RVal v = GenExpr(t->Elems[i].get());
                    std::string fp = N();
                    I(fp + " = getelementptr inbounds %" + tn + ", ptr " + tmp + ", i32 0, i32 " +
                      std::to_string(i));
                    I("store " + v.T + " " + v.V + ", ptr " + fp);
                }
                std::string r = N();
                I(r + " = load %" + tn + ", ptr " + tmp);
                return {"%" + tn, r, t->Ty};
            }
            case Expr::Kind::Member: {
                auto* m = static_cast<MemberExpr*>(e);
                if (m->IsEnumUnit) {
                    auto ed = EnumByName.find(m->EnumKey);
                    if (ed == EnumByName.end())
                        throw Unsupported{m->Sp, "enum desconocido en el backend llvm"};
                    int vi = VariantIndex(ed->second, m->Name);
                    std::string eid = Q(CppId(m->EnumKey));
                    std::string t = AllocaT("%" + eid);
                    I("store %" + eid + " zeroinitializer, ptr " + t);
                    std::string tp = N();
                    I(tp + " = getelementptr inbounds %" + eid + ", ptr " + t +
                      ", i32 0, i32 0");
                    I("store i64 " + std::to_string(vi) + ", ptr " + tp);
                    std::string r = N();
                    I(r + " = load %" + eid + ", ptr " + t);
                    return {"%" + Q(CppId(m->EnumKey)), r, e->Ty};
                }
                LVal lv = GenLval(e);
                std::string lt = LT(e->Ty, e->Sp);
                std::string r = N();
                I(r + " = load " + lt + ", ptr " + lv.Ptr);
                return {lt, r, e->Ty};
            }
        }
        throw Unsupported{e->Sp, "expresión no soportada por el backend llvm"};
    }

    RVal GenIndex(Expr* e) {
        auto* ix = static_cast<IndexExpr*>(e);
        if (ix->TupleIndex) {
            LVal lv = GenLval(e);
            std::string lt = LT(e->Ty, e->Sp);
            std::string r = N();
            I(r + " = load " + lt + ", ptr " + lv.Ptr);
            return {lt, r, e->Ty};
        }
        Type bt = ix->Base->Ty.Pointee();
        RVal b = GenExpr(ix->Base.get());
        RVal iv = GenExpr(ix->Idx.get());
        std::string nom = StrConst(BaseName(ix->Base.get()));
        if (bt.IsArray()) {
            Type et = bt.Sub[0];
            std::string sz = TypeSize(et, ix->Sp);
            std::string slot = N();
            I(slot + " = call ptr @sy_arr_slot(ptr " + b.V + ", i64 " + iv.V + ", i64 " + sz +
              ", ptr " + nom + ")");
            std::string lt = LT(et, ix->Sp);
            std::string r = N();
            I(r + " = load " + lt + ", ptr " + slot);
            return {lt, r, e->Ty};
        }
        if (bt.IsMap()) {
            Type kt = bt.Sub[0], vt = bt.Sub[1];
            std::string vsz = TypeSize(vt, ix->Sp);
            std::string slot;
            if (kt.IsStr()) {
                slot = N();
                I(slot + " = call ptr @sy_map_slot_str(ptr " + b.V + ", ptr " + iv.V +
                  ", i64 " + vsz + ", ptr " + nom + ")");
            } else {
                slot = N();
                I(slot + " = call ptr @sy_map_slot_int(ptr " + b.V + ", i64 " + iv.V +
                  ", i64 " + vsz + ", ptr " + nom + ")");
            }
            std::string lt = LT(vt, ix->Sp);
            std::string r = N();
            I(r + " = load " + lt + ", ptr " + slot);
            return {lt, r, e->Ty};
        }
        throw Unsupported{e->Sp, "índice no soportado por el backend llvm"};
    }

    RVal GenLogic(BinaryExpr* b) {
        bool isAnd = (b->Op == "and");
        std::string t = AllocaT("i1");
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
            bool agg = at.IsStruct() || at.IsEnum() || at.IsTuple();
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
                    if (c->Args.size() == 2) {
                        RVal v = GenExpr(c->Args[0].Value.get());
                        if (v.Ty.IsInt()) v = CastVal(v, Type::Float());
                        RVal d = GenExpr(c->Args[1].Value.get());
                        std::string r = N();
                        I(r + " = call ptr @sy_f64_prec(double " + v.V + ", i64 " + d.V + ")");
                        std::string w = N();
                        I(w + " = call ptr @sy_str_from_c(ptr " + r + ")");
                        return {"ptr", w, Type::Str()};
                    }
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
                if (n == "Clone") {
                    RVal v = GenExpr(c->Args[0].Value.get());
                    if (v.Ty.IsStr()) {
                        std::string r = N();
                        I(r + " = call ptr @sy_str_clona(ptr " + v.V + ")");
                        return {"ptr", r, v.Ty};
                    }
                    if (v.Ty.IsArray()) {
                        Type et = v.Ty.Sub[0];
                        std::string sz = TypeSize(et, c->Sp);
                        std::string es = et.IsStr() ? "1" : "0";
                        std::string r = N();
                        I(r + " = call ptr @sy_arr_copia(ptr " + v.V + ", i64 " + sz + ", i1 " +
                          es + ")");
                        return {"ptr", r, v.Ty};
                    }
                    if (v.Ty.IsMap()) {
                        Type kt = v.Ty.Sub[0], vt = v.Ty.Sub[1];
                        std::string kk = kt.IsStr() ? "0" : "1";
                        std::string vsz = TypeSize(vt, c->Sp);
                        std::string vs = vt.IsStr() ? "1" : "0";
                        std::string r = N();
                        I(r + " = call ptr @sy_map_copia(ptr " + v.V + ", i64 " + kk + ", i64 " +
                          vsz + ", i1 " + vs + ")");
                        return {"ptr", r, v.Ty};
                    }
                    return v;
                }
                if (n == "Afirma") {
                    RVal cond = GenExpr(c->Args[0].Value.get());
                    RVal msg = GenExpr(c->Args[1].Value.get());
                    std::string z = N();
                    I(z + " = zext i1 " + cond.V + " to i32");
                    std::string d = N();
                    I(d + " = getelementptr inbounds %sylt.str, ptr " + msg.V +
                      ", i32 0, i32 1");
                    std::string db = N();
                    I(db + " = load ptr, ptr " + d);
                    I("call void @sy_afirma(i32 " + z + ", ptr " + db + ", i64 " +
                      std::to_string(c->Sp.Line) + ")");
                    return {"void", "", Type::Void()};
                }
                if (n == "TiempoMs") {
                    std::string r = N();
                    I(r + " = call double @sy_mono_ms()");
                    return {"double", r, Type::Float()};
                }
                if (n == "HiloLanzar") return GenHiloLanzar(c);
                throw Unsupported{c->Sp, "función integrada no soportada por el backend llvm"};
            }
            case CallExpr::Target::Fn: {
                auto f = FnByName.find(c->TargetName);
                if (ForeignDecls.count(c->TargetName)) return GenForeignCall(c);
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
                    I("call void @" + Q(c->TargetName) + "(" + sig + ")");
                    return {"void", "", Type::Void()};
                }
                std::string r = N();
                I(r + " = call " + ret + " @" + Q(c->TargetName) + "(" + sig + ")");
                return {ret, r, c->Ty};
            }
            case CallExpr::Target::EnumCtor: {
                auto ed = EnumByName.find(c->TargetName);
                if (ed == EnumByName.end())
                    throw Unsupported{c->Sp, "enum desconocido en el backend llvm"};
                int vi = VariantIndex(ed->second, c->VariantName);
                auto args = GenArgs(c);
                std::string tid = Q(CppId(c->TargetName));
                std::string t = N();
                I(t + " = alloca %" + tid);
                I("store %" + tid + " zeroinitializer, ptr " + t);
                std::string tp = N();
                I(tp + " = getelementptr inbounds %" + tid + ", ptr " + t +
                  ", i32 0, i32 0");
                I("store i64 " + std::to_string(vi) + ", ptr " + tp);
                EnumVariant& v = ed->second->Variants[vi];
                CurModule = ed->second->Module;
                for (size_t i = 0; i < args.size() && i < v.Fields.size(); i++) {
                    Type ft = TypeOf(v.Fields[i].Ty);
                    int slot = 1 + EnumFieldPos[c->TargetName][v.Fields[i].Name];
                    std::string fp = N();
                    I(fp + " = getelementptr inbounds %" + tid + ", ptr " + t +
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
                I(r + " = load %" + tid + ", ptr " + t);
                return {"%" + tid, r, c->Ty};
            }
            case CallExpr::Target::StructInit: {
                auto it = TypeByName.find(c->TargetName);
                if (it == TypeByName.end())
                    throw Unsupported{c->Sp, "struct desconocido en el backend llvm"};
                TypeDecl* td = it->second;
                CurModule = td->Module;
                std::string tid = Q(CppId(c->TargetName));
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
                I(t + " = alloca %" + tid);
                for (size_t i = 0; i < td->Fields.size(); i++) {
                    Type ft = TypeOf(td->Fields[i].Ty);
                    std::string lt = LT(ft, td->Fields[i].Sp);
                    std::string fp = N();
                    I(fp + " = getelementptr inbounds %" + tid + ", ptr " + t +
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
                I(r + " = load %" + tid + ", ptr " + t);
                return {"%" + tid, r, c->Ty};
            }
            case CallExpr::Target::Method: {
                auto* m = static_cast<MemberExpr*>(c->Callee.get());
                Type bt = m->Base->Ty.Pointee();
                if (bt.IsStr()) return GenStringMethod(c, m);
                if (bt.IsArray()) return GenCollMethod(c, m, bt);
                if (bt.IsMap()) return GenCollMethod(c, m, bt);
                LVal recv = GenLval(m->Base.get());
                auto args = GenArgs(c);
                std::string full = Q(CppId(Canoniza(bt.Name)) + "__" + c->TargetName);
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

    RVal GenCollMethod(CallExpr* c, MemberExpr* m, Type bt) {
        const std::string& n = c->TargetName;
        RVal b = GenExpr(m->Base.get());
        if (bt.IsArray()) {
            Type et = bt.Sub[0];
            if (n == "Len") {
                std::string r = N();
                I(r + " = call i64 @sy_arr_len(ptr " + b.V + ")");
                return {"i64", r, Type::Int()};
            }
            if (n == "Push") {
                RVal v = GenExpr(c->Args[0].Value.get());
                v = CastVal(v, et);
                std::string sz = TypeSize(et, c->Sp);
                std::string tmp = AllocaT(LT(et, c->Sp));
                I("store " + v.T + " " + v.V + ", ptr " + tmp);
                I("call ptr @sy_arr_push(ptr " + b.V + ", ptr " + tmp + ", i64 " + sz + ")");
                return {"void", "", Type::Void()};
            }
            if (n == "Pop") {
                std::string sz = TypeSize(et, c->Sp);
                std::string slot = N();
                I(slot + " = call ptr @sy_arr_pop(ptr " + b.V + ", i64 " + sz + ")");
                std::string lt = LT(et, c->Sp);
                std::string r = N();
                I(r + " = load " + lt + ", ptr " + slot);
                return {lt, r, et};
            }
            if (n == "Contiene") {
                RVal v = GenExpr(c->Args[0].Value.get());
                v = CastVal(v, et);
                std::string sz = TypeSize(et, c->Sp);
                std::string r = N();
                if (et.IsStr()) {
                    I(r + " = call i1 @sy_arr_tiene_str(ptr " + b.V + ", ptr " + v.V + ")");
                } else {
                    std::string tmp = AllocaT(LT(et, c->Sp));
                    I("store " + v.T + " " + v.V + ", ptr " + tmp);
                    I(r + " = call i1 @sy_arr_tiene(ptr " + b.V + ", ptr " + tmp + ", i64 " +
                      sz + ")");
                }
                return {"i1", r, Type::Bool()};
            }
        } else {
            Type kt = bt.Sub[0], vt = bt.Sub[1];
            std::string vsz = TypeSize(vt, c->Sp);
            if (n == "Len") {
                std::string r = N();
                I(r + " = call i64 @sy_map_len(ptr " + b.V + ")");
                return {"i64", r, Type::Int()};
            }
            if (n == "Contiene") {
                RVal k = GenExpr(c->Args[0].Value.get());
                std::string r = N();
                if (kt.IsStr())
                    I(r + " = call i1 @sy_map_tiene_str(ptr " + b.V + ", ptr " + k.V + ")");
                else
                    I(r + " = call i1 @sy_map_tiene_int(ptr " + b.V + ", i64 " + k.V + ")");
                return {"i1", r, Type::Bool()};
            }
            if (n == "Claves") {
                std::string kk = kt.IsStr() ? "0" : "1";
                std::string r = N();
                I(r + " = call ptr @sy_map_claves(ptr " + b.V + ", i64 " + kk + ")");
                return {"ptr", r, Type::ArrayOf(kt)};
            }
        }
        throw Unsupported{c->Sp,
                          "el método `" + n + "` de colecciones no está soportado todavía por "
                          "el backend llvm"};
    }

    RVal GenForeignCall(CallExpr* c) {
        auto f = ForeignDecls.find(c->TargetName);
        if (f == ForeignDecls.end())
            throw Unsupported{c->Sp, "llamada externa desconocida en el backend llvm"};
        FnDecl* fd = f->second;
        std::vector<Param*> ps;
        for (auto& p : fd->Params)
            if (!p.IsSelf) ps.push_back(&p);
        std::vector<std::pair<std::string, std::string>> args;
        for (size_t i = 0; i < c->Args.size(); i++) {
            Arg& a = c->Args[i];
            Type pt = i < ps.size() ? TypeOf(ps[i]->Ty) : a.Value->Ty;
            RVal v = GenExpr(a.Value.get());
            if (i < c->ArgCastFloat.size() && c->ArgCastFloat[i]) {
                std::string r = N();
                I(r + " = sitofp i64 " + v.V + " to double");
                v = {"double", r, Type::Float()};
            }
            if (pt.IsStr()) {
                std::string d = N();
                I(d + " = getelementptr inbounds %sylt.str, ptr " + v.V + ", i32 0, i32 1");
                std::string db = N();
                I(db + " = load ptr, ptr " + d);
                args.push_back({"ptr", db});
            } else if (pt.IsBool()) {
                std::string z = N();
                I(z + " = zext i1 " + v.V + " to i32");
                args.push_back({"i32", z});
            } else if (pt.IsInt() || pt.IsFloat() || pt.IsArray() || pt.IsMap()) {
                args.push_back({v.T, v.V});
            } else {
                throw Unsupported{a.Value->Sp,
                                  "los argumentos externos de este tipo no están soportados "
                                  "por el backend llvm"};
            }
        }
        std::string ret = "void";
        Type rt = Type::Void();
        if (fd->HasRet) rt = TypeOf(fd->Ret);
        if (rt.IsBool()) ret = "i32";
        else if (!rt.IsVoid()) ret = LT(rt, c->Sp);
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
        if (rt.IsStr()) {
            std::string w = N();
            I(w + " = call ptr @sy_str_from_c(ptr " + r + ")");
            return {"ptr", w, Type::Str()};
        }
        if (rt.IsBool()) {
            std::string t = N();
            I(t + " = trunc i32 " + r + " to i1");
            return {"i1", t, Type::Bool()};
        }
        return {ret, r, rt};
    }

    RVal GenHiloLanzar(CallExpr* c) {
        if (c->SpawnSite < 0 || c->SpawnTarget.empty())
            throw Unsupported{c->Sp, "lanzar hilos no está soportado por el backend llvm"};
        std::string fn = "sylt.tramp." + std::to_string(c->SpawnSite);
        std::string pk = N();
        I(pk + " = call ptr @sy_paquete_nuevo()");
        for (size_t i = 1; i < c->Args.size(); i++) {
            RVal v = GenExpr(c->Args[i].Value.get());
            if (i < c->ArgCastFloat.size() && c->ArgCastFloat[i]) {
                std::string r = N();
                I(r + " = sitofp i64 " + v.V + " to double");
                v = {"double", r, Type::Float()};
            }
            Type at = c->Args[i].Value->Ty;
            std::string idx = std::to_string(i - 1);
            if (at.IsInt()) {
                I("call ptr @sy_paquete_int(ptr " + pk + ", i64 " + v.V + ")");
            } else if (at.IsFloat()) {
                I("call ptr @sy_paquete_float(ptr " + pk + ", double " + v.V + ")");
            } else if (at.IsBool()) {
                std::string z = N();
                I(z + " = zext i1 " + v.V + " to i32");
                I("call ptr @sy_paquete_bool(ptr " + pk + ", i32 " + z + ")");
            } else if (at.IsStr()) {
                I("call ptr @sy_paquete_str(ptr " + pk + ", ptr " + v.V + ")");
            }
        }
        std::string id = N();
        I(id + " = call i64 @sy_hilo_lanzar(ptr @" + fn + ", ptr " + pk + ")");
        return {"i64", id, Type::Int()};
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
                if (s->Destructure) {
                    RVal v = GenExpr(s->Init.get());
                    std::string tn = TupleType(s->VarType, s->Sp);
                    std::string tmp = AllocaT("%" + tn);
                    I("store " + v.T + " " + v.V + ", ptr " + tmp);
                    for (size_t i = 0; i < s->Names.size() && i < s->VarType.Sub.size(); i++) {
                        if (s->Names[i] == "_") continue;
                        Type et = s->VarType.Sub[i];
                        std::string fp = N();
                        I(fp + " = getelementptr inbounds %" + tn + ", ptr " + tmp +
                          ", i32 0, i32 " + std::to_string(i));
                        std::string lt = LT(et, s->Sp);
                        std::string lv = N();
                        I(lv + " = load " + lt + ", ptr " + fp);
                        std::string p = AllocaT(lt);
                        I("store " + lt + " " + lv + ", ptr " + p);
                        LetBind(s->Names[i], p);
                    }
                    return;
                }
                RVal v = GenExpr(s->Init.get());
                if (s->Names[0] == "_") return;
                Type vt = s->VarType;
                std::string lt = LT(vt, s->Sp);
                v = CastVal(v, vt);
                std::string p = AllocaT(lt);
                I("store " + lt + " " + v.V + ", ptr " + p);
                LetBind(s->Names[0], p);
                return;
            }
            case Stmt::Kind::Assign: {
                auto* s = static_cast<AssignStmt*>(st);
                Expr* t = s->Target.get();
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
                if (s->IsRange) {
                    RVal lo = GenExpr(s->Low.get());
                    RVal hi = GenExpr(s->High.get());
                    std::string lp = AllocaT("i64");
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
                Type it = s->Low->Ty.Pointee();
                RVal b = GenExpr(s->Low.get());
                std::string cond = Lbl("forcond"), body = Lbl("forbody");
                std::string step = Lbl("forstep"), end = Lbl("forend");
                std::string len = N();
                if (it.IsArray()) {
                    I(len + " = call i64 @sy_arr_len(ptr " + b.V + ")");
                } else if (it.IsMap()) {
                    I(len + " = call i64 @sy_map_len(ptr " + b.V + ")");
                } else {
                    throw Unsupported{s->Sp,
                                      "solo se itera sobre arrays y mapas en el backend llvm"};
                }
                std::string ip = AllocaT("i64");
                I("store i64 0, ptr " + ip);
                Term("br label %" + cond);
                Open(cond);
                std::string i1 = N(), c = N();
                I(i1 + " = load i64, ptr " + ip);
                I(c + " = icmp slt i64 " + i1 + ", " + len);
                Term("br i1 " + c + ", label %" + body + ", label %" + end);
                Open(body);
                size_t mark = Undo.size();
                if (it.IsArray()) {
                    Type et = it.Sub[0];
                    std::string sz = TypeSize(et, s->Sp);
                    std::string nom = StrConst(BaseName(s->Low.get()));
                    std::string slot = N();
                    I(slot + " = call ptr @sy_arr_slot(ptr " + b.V + ", i64 " + i1 + ", i64 " +
                      sz + ", ptr " + nom + ")");
                    std::string vp = AllocaT(LT(et, s->Sp));
                    if (et.IsStr()) {
                        std::string h0 = N(), h = N();
                        I(h0 + " = load ptr, ptr " + slot);
                        I(h + " = call ptr @sy_str_clona(ptr " + h0 + ")");
                        I("store ptr " + h + ", ptr " + vp);
                    } else {
                        std::string v = N();
                        I(v + " = load " + LT(et, s->Sp) + ", ptr " + slot);
                        I("store " + LT(et, s->Sp) + " " + v + ", ptr " + vp);
                    }
                    LetBind(s->Var, vp);
                } else {
                    Type kt = it.Sub[0];
                    std::string vp = AllocaT(kt.IsStr() ? "ptr" : "i64");
                    if (kt.IsStr()) {
                        std::string kh = N(), h = N();
                        I(kh + " = call ptr @sy_map_clave_str(ptr " + b.V + ", i64 " + i1 + ")");
                        I(h + " = call ptr @sy_str_clona(ptr " + kh + ")");
                        I("store ptr " + h + ", ptr " + vp);
                    } else {
                        std::string kv = N();
                        I(kv + " = call i64 @sy_map_clave_int(ptr " + b.V + ", i64 " + i1 + ")");
                        I("store i64 " + kv + ", ptr " + vp);
                    }
                    LetBind(s->Var, vp);
                }
                LoopStack.push_back({step, end});
                GenBlockInner(s->Body);
                LoopStack.pop_back();
                ScopeRestore(mark);
                if (!Closed.back()) Term("br label %" + step);
                Open(step);
                std::string i2 = N(), i3 = N();
                I(i2 + " = load i64, ptr " + ip);
                I(i3 + " = add i64 " + i2 + ", 1");
                I("store i64 " + i3 + ", ptr " + ip);
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
        std::string sp = AllocaT(sv.T);
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
                I(tagp + " = getelementptr inbounds %" + Q(CppId(enumKey)) + ", ptr " + sp +
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
                    I(bp + " = getelementptr inbounds %" + Q(CppId(enumKey)) + ", ptr " + sp +
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
        if (!Prog->ForeignIncludes.empty())
            throw Unsupported{Prog->Sp,
                              "las funciones externas con `include` de C no están soportadas "
                              "todavía por el backend llvm"};
        SitioN = 0;
        for (auto& fd : Prog->Fns) {
            if (fd->Foreign) continue;
            AsignaSitiosBloque(fd->Body);
        }
        for (auto& td : Prog->Types)
            for (auto& m : td->Methods) {
                if (!td->TypeParams.empty()) continue;
                AsignaSitiosBloque(m->Body);
            }
        EmitTypes();
        EmitDeclares();
        EmitStrOfProtos();
        for (auto& fd : Prog->Fns)
            if (!fd->Foreign) EmitFnDecl(*fd);
        for (auto& td : Prog->Types)
            for (auto& m : td->Methods) {
                if (!td->TypeParams.empty()) continue;
                EmitFnDecl(*m, Q(CppId(Key(td->Module, td->Name)) + "__" + m->Name));
            }
        for (size_t i = 0; i < PendColl.size(); i++)
            EmitCollStr(PendColl[i].first, PendColl[i].second);
        EmitTrampolines();
        EmitMain();
    }

    int SitioN = 0;

    void AsignaSitiosBloque(const Block& b) {
        for (auto& s : b.Stmts) AsignaSitiosStmt(s.get());
    }

    void AsignaSitiosStmt(Stmt* st) {
        switch (st->K) {
            case Stmt::Kind::Let:
                AsignaSitiosExpr(static_cast<LetStmt*>(st)->Init.get());
                return;
            case Stmt::Kind::Assign: {
                auto* s = static_cast<AssignStmt*>(st);
                AsignaSitiosExpr(s->Target.get());
                AsignaSitiosExpr(s->Value.get());
                return;
            }
            case Stmt::Kind::Expr:
                AsignaSitiosExpr(static_cast<ExprStmt*>(st)->Sub.get());
                return;
            case Stmt::Kind::Return: {
                auto* s = static_cast<ReturnStmt*>(st);
                if (s->Value) AsignaSitiosExpr(s->Value.get());
                return;
            }
            case Stmt::Kind::If: {
                auto* s = static_cast<IfStmt*>(st);
                AsignaSitiosExpr(s->Cond.get());
                AsignaSitiosBloque(s->Then);
                for (auto& br : s->Elifs) {
                    AsignaSitiosExpr(br.Cond.get());
                    AsignaSitiosBloque(br.Body);
                }
                if (s->HasElse) AsignaSitiosBloque(s->Else);
                return;
            }
            case Stmt::Kind::While: {
                auto* s = static_cast<WhileStmt*>(st);
                AsignaSitiosExpr(s->Cond.get());
                AsignaSitiosBloque(s->Body);
                return;
            }
            case Stmt::Kind::For: {
                auto* s = static_cast<ForStmt*>(st);
                AsignaSitiosExpr(s->Low.get());
                if (s->IsRange && s->High) AsignaSitiosExpr(s->High.get());
                AsignaSitiosBloque(s->Body);
                return;
            }
            case Stmt::Kind::Match: {
                auto* s = static_cast<MatchStmt*>(st);
                AsignaSitiosExpr(s->Subject.get());
                for (auto& arm : s->Arms) {
                    if (arm.Pat.Lit) AsignaSitiosExpr(arm.Pat.Lit.get());
                    AsignaSitiosBloque(arm.Body);
                }
                return;
            }
            default:
                return;
        }
    }

    void AsignaSitiosExpr(Expr* e) {
        if (!e) return;
        switch (e->K) {
            case Expr::Kind::Call: {
                auto* c = static_cast<CallExpr*>(e);
                for (auto& a : c->Args) AsignaSitiosExpr(a.Value.get());
                if (c->Resolved == CallExpr::Target::Builtin &&
                    c->TargetName == "HiloLanzar" && !c->SpawnTarget.empty())
                    c->SpawnSite = SitioN++;
                return;
            }
            case Expr::Kind::Unary:
                AsignaSitiosExpr(static_cast<UnaryExpr*>(e)->Sub.get());
                return;
            case Expr::Kind::Binary: {
                auto* b = static_cast<BinaryExpr*>(e);
                AsignaSitiosExpr(b->L.get());
                AsignaSitiosExpr(b->R.get());
                return;
            }
            case Expr::Kind::Index: {
                auto* ix = static_cast<IndexExpr*>(e);
                AsignaSitiosExpr(ix->Base.get());
                AsignaSitiosExpr(ix->Idx.get());
                return;
            }
            case Expr::Kind::Member:
                AsignaSitiosExpr(static_cast<MemberExpr*>(e)->Base.get());
                return;
            case Expr::Kind::StrLit:
                for (auto& p : static_cast<StrLitExpr*>(e)->Parts)
                    if (!p.IsText) AsignaSitiosExpr(p.Sub.get());
                return;
            case Expr::Kind::ArrayLit:
                for (auto& x : static_cast<ArrayLitExpr*>(e)->Elems) AsignaSitiosExpr(x.get());
                return;
            case Expr::Kind::MapLit:
                for (auto& p : static_cast<MapLitExpr*>(e)->Pairs) {
                    AsignaSitiosExpr(p.Key.get());
                    AsignaSitiosExpr(p.Val.get());
                }
                return;
            case Expr::Kind::TupleLit:
                for (auto& x : static_cast<TupleLitExpr*>(e)->Elems) AsignaSitiosExpr(x.get());
                return;
            default:
                return;
        }
    }

    void EmitTrampolines() {
        for (size_t site = 0; site < 100000; site++) {
            CallExpr* hallado = nullptr;
            std::string objetivo;
            for (auto& fd : Prog->Fns) {
                if (fd->Foreign) continue;
                hallado = BuscaTramp(fd->Body, site, objetivo);
                if (hallado) break;
            }
            if (!hallado)
                for (auto& td : Prog->Types) {
                    if (!td->TypeParams.empty()) continue;
                    for (auto& m : td->Methods) {
                        hallado = BuscaTramp(m->Body, site, objetivo);
                        if (hallado) break;
                    }
                    if (hallado) break;
                }
            if (!hallado) break;
            std::vector<Type> ts;
            for (size_t i = 1; i < hallado->Args.size(); i++)
                ts.push_back(hallado->Args[i].Value->Ty);
            std::string sig = "define private ptr @sylt.tramp." + std::to_string(site) +
                              "(ptr %pk) {\n";
            for (size_t i = 0; i < ts.size(); i++) {
                std::string a = "";
                if (ts[i].IsInt()) {
                    a = N();
                    sig += "  " + a + " = call i64 @sy_paquete_dame_int(ptr %pk, i64 " +
                           std::to_string(i) + ")\n";
                } else if (ts[i].IsFloat()) {
                    a = N();
                    sig += "  " + a + " = call double @sy_paquete_dame_float(ptr %pk, i64 " +
                           std::to_string(i) + ")\n";
                } else if (ts[i].IsBool()) {
                    std::string b32 = N();
                    sig += "  " + b32 + " = call i32 @sy_paquete_dame_bool(ptr %pk, i64 " +
                           std::to_string(i) + ")\n";
                    a = N();
                    sig += "  " + a + " = trunc i32 " + b32 + " to i1\n";
                } else if (ts[i].IsStr()) {
                    a = N();
                    sig += "  " + a + " = call ptr @sy_paquete_dame_str(ptr %pk, i64 " +
                           std::to_string(i) + ")\n";
                }
                ls_args.push_back(a);
            }
            std::string call = "  call void @" + Q(objetivo) + "(";
            for (size_t i = 0; i < ls_args.size(); i++) {
                if (i) call += ", ";
                call += LT(ts[i], hallado->Sp) + " " + ls_args[i];
            }
            call += ")\n";
            sig += call;
            sig += "  call void @sy_paquete_libera(ptr %pk)\n";
            sig += "  ret ptr null\n}\n\n";
            Bs << sig;
            ls_args.clear();
        }
    }

    std::vector<std::string> ls_args;

    CallExpr* BuscaTramp(const Block& b, size_t site, std::string& objetivo) {
        for (auto& s : b.Stmts) {
            CallExpr* r = BuscaTrampStmt(s.get(), site, objetivo);
            if (r) return r;
        }
        return nullptr;
    }

    CallExpr* BuscaTrampStmt(Stmt* st, size_t site, std::string& objetivo) {
        switch (st->K) {
            case Stmt::Kind::Let:
                return BuscaTrampExpr(static_cast<LetStmt*>(st)->Init.get(), site, objetivo);
            case Stmt::Kind::Assign: {
                auto* s = static_cast<AssignStmt*>(st);
                CallExpr* r = BuscaTrampExpr(s->Target.get(), site, objetivo);
                if (r) return r;
                return BuscaTrampExpr(s->Value.get(), site, objetivo);
            }
            case Stmt::Kind::Expr:
                return BuscaTrampExpr(static_cast<ExprStmt*>(st)->Sub.get(), site, objetivo);
            case Stmt::Kind::Return: {
                auto* s = static_cast<ReturnStmt*>(st);
                return s->Value ? BuscaTrampExpr(s->Value.get(), site, objetivo) : nullptr;
            }
            case Stmt::Kind::If: {
                auto* s = static_cast<IfStmt*>(st);
                CallExpr* r = BuscaTrampExpr(s->Cond.get(), site, objetivo);
                if (r) return r;
                r = BuscaTramp(s->Then, site, objetivo);
                if (r) return r;
                for (auto& br : s->Elifs) {
                    r = BuscaTrampExpr(br.Cond.get(), site, objetivo);
                    if (r) return r;
                    r = BuscaTramp(br.Body, site, objetivo);
                    if (r) return r;
                }
                if (s->HasElse) return BuscaTramp(s->Else, site, objetivo);
                return nullptr;
            }
            case Stmt::Kind::While: {
                auto* s = static_cast<WhileStmt*>(st);
                CallExpr* r = BuscaTrampExpr(s->Cond.get(), site, objetivo);
                if (r) return r;
                return BuscaTramp(s->Body, site, objetivo);
            }
            case Stmt::Kind::For: {
                auto* s = static_cast<ForStmt*>(st);
                CallExpr* r = BuscaTrampExpr(s->Low.get(), site, objetivo);
                if (r) return r;
                if (s->IsRange && s->High) {
                    r = BuscaTrampExpr(s->High.get(), site, objetivo);
                    if (r) return r;
                }
                return BuscaTramp(s->Body, site, objetivo);
            }
            case Stmt::Kind::Match: {
                auto* s = static_cast<MatchStmt*>(st);
                CallExpr* r = BuscaTrampExpr(s->Subject.get(), site, objetivo);
                if (r) return r;
                for (auto& arm : s->Arms) {
                    if (arm.Pat.Lit) {
                        r = BuscaTrampExpr(arm.Pat.Lit.get(), site, objetivo);
                        if (r) return r;
                    }
                    r = BuscaTramp(arm.Body, site, objetivo);
                    if (r) return r;
                }
                return nullptr;
            }
            default:
                return nullptr;
        }
    }

    CallExpr* BuscaTrampExpr(Expr* e, size_t site, std::string& objetivo) {
        if (!e) return nullptr;
        if (e->K == Expr::Kind::Call) {
            auto* c = static_cast<CallExpr*>(e);
            if (c->Resolved == CallExpr::Target::Builtin &&
                c->TargetName == "HiloLanzar" && c->SpawnSite == (int)site) {
                objetivo = c->SpawnTarget;
                return c;
            }
            for (auto& a : c->Args) {
                CallExpr* r = BuscaTrampExpr(a.Value.get(), site, objetivo);
                if (r) return r;
            }
            return nullptr;
        }
        switch (e->K) {
            case Expr::Kind::Unary:
                return BuscaTrampExpr(static_cast<UnaryExpr*>(e)->Sub.get(), site, objetivo);
            case Expr::Kind::Binary: {
                auto* b = static_cast<BinaryExpr*>(e);
                CallExpr* r = BuscaTrampExpr(b->L.get(), site, objetivo);
                if (r) return r;
                return BuscaTrampExpr(b->R.get(), site, objetivo);
            }
            case Expr::Kind::Index: {
                auto* ix = static_cast<IndexExpr*>(e);
                CallExpr* r = BuscaTrampExpr(ix->Base.get(), site, objetivo);
                if (r) return r;
                return BuscaTrampExpr(ix->Idx.get(), site, objetivo);
            }
            case Expr::Kind::Member:
                return BuscaTrampExpr(static_cast<MemberExpr*>(e)->Base.get(), site, objetivo);
            case Expr::Kind::StrLit:
                for (auto& p : static_cast<StrLitExpr*>(e)->Parts)
                    if (!p.IsText) {
                        CallExpr* r = BuscaTrampExpr(p.Sub.get(), site, objetivo);
                        if (r) return r;
                    }
                return nullptr;
            case Expr::Kind::ArrayLit:
                for (auto& x : static_cast<ArrayLitExpr*>(e)->Elems) {
                    CallExpr* r = BuscaTrampExpr(x.get(), site, objetivo);
                    if (r) return r;
                }
                return nullptr;
            case Expr::Kind::MapLit:
                for (auto& p : static_cast<MapLitExpr*>(e)->Pairs) {
                    CallExpr* r = BuscaTrampExpr(p.Key.get(), site, objetivo);
                    if (r) return r;
                    r = BuscaTrampExpr(p.Val.get(), site, objetivo);
                    if (r) return r;
                }
                return nullptr;
            case Expr::Kind::TupleLit:
                for (auto& x : static_cast<TupleLitExpr*>(e)->Elems) {
                    CallExpr* r = BuscaTrampExpr(x.get(), site, objetivo);
                    if (r) return r;
                }
                return nullptr;
            default:
                return nullptr;
        }
    }

    void EmitTypes() {
        for (auto& td : Prog->Types) {
            if (!td->TypeParams.empty()) continue;
            std::string key = CppId(Key(td->Module, td->Name));
            CurModule = td->Module;
            std::string s = "%" + Q(key) + " = type { ";
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
            std::string s = "%" + Q(CppId(key)) + " = type { i64";
            for (size_t i = 0; i < order.size(); i++) {
                s += ", " + LT(kinds[order[i]], ed->Sp);
                EnumFieldPos[key][order[i]] = (int)i;
            }
            Ts << s + " }\n";
        }
    }

    void EmitDeclares() {
        Ds << "@sylt_jb = external global [64 x i64]\n";
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
        Ds << "declare i32 @setjmp(ptr) #0\n";
        Ds << "declare void @longjmp(ptr, i32) #1\n";
        Ds << "declare void @sy_test_arma()\n";
        Ds << "declare void @sy_test_ok(ptr)\n";
        Ds << "declare void @sy_test_mal(ptr)\n";
        Ds << "declare i64 @sy_test_resumen(i64)\n";
        Ds << "declare void @sy_afirma(i32, ptr, i64)\n";
        Ds << "declare ptr @sy_arr_nuevo(i64)\n";
        Ds << "declare void @sy_arr_push(ptr, ptr, i64)\n";
        Ds << "declare i64 @sy_arr_len(ptr)\n";
        Ds << "declare ptr @sy_arr_slot(ptr, i64, i64, ptr)\n";
        Ds << "declare ptr @sy_arr_pop(ptr, i64)\n";
        Ds << "declare i1 @sy_arr_tiene(ptr, ptr, i64)\n";
        Ds << "declare i1 @sy_arr_tiene_str(ptr, ptr)\n";
        Ds << "declare ptr @sy_arr_copia(ptr, i64, i1)\n";
        Ds << "declare ptr @sy_map_nuevo(i64, i64)\n";
        Ds << "declare i64 @sy_map_len(ptr)\n";
        Ds << "declare i1 @sy_map_tiene_str(ptr, ptr)\n";
        Ds << "declare i1 @sy_map_tiene_int(ptr, i64)\n";
        Ds << "declare ptr @sy_map_slot_str(ptr, ptr, i64, ptr)\n";
        Ds << "declare ptr @sy_map_slot_int(ptr, i64, i64, ptr)\n";
        Ds << "declare ptr @sy_map_slot_ins_str(ptr, ptr, i64)\n";
        Ds << "declare ptr @sy_map_slot_ins_int(ptr, i64, i64)\n";
        Ds << "declare ptr @sy_map_pon_str(ptr, ptr, ptr, i64)\n";
        Ds << "declare ptr @sy_map_pon_int(ptr, i64, ptr, i64)\n";
        Ds << "declare ptr @sy_map_clave_str(ptr, i64)\n";
        Ds << "declare i64 @sy_map_clave_int(ptr, i64)\n";
        Ds << "declare ptr @sy_map_valor(ptr, i64, i64)\n";
        Ds << "declare ptr @sy_map_copia(ptr, i64, i64, i1)\n";
        Ds << "declare ptr @sy_map_claves(ptr, i64)\n";
        Ds << "declare ptr @sy_str_clona(ptr)\n";
        Ds << "declare ptr @sy_str_from_c(ptr)\n";
        Ds << "declare ptr @sy_f64_prec(double, i64)\n";
        Ds << "declare double @sy_mono_ms()\n";
        Ds << "declare ptr @sy_paquete_nuevo()\n";
        Ds << "declare ptr @sy_paquete_int(ptr, i64)\n";
        Ds << "declare ptr @sy_paquete_float(ptr, double)\n";
        Ds << "declare ptr @sy_paquete_bool(ptr, i32)\n";
        Ds << "declare ptr @sy_paquete_str(ptr, ptr)\n";
        Ds << "declare i64 @sy_paquete_dame_int(ptr, i64)\n";
        Ds << "declare double @sy_paquete_dame_float(ptr, i64)\n";
        Ds << "declare i32 @sy_paquete_dame_bool(ptr, i64)\n";
        Ds << "declare ptr @sy_paquete_dame_str(ptr, i64)\n";
        Ds << "declare void @sy_paquete_libera(ptr)\n";
        Ds << "declare i64 @sy_hilo_lanzar(ptr, ptr)\n";
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
        if (t.IsStruct() || t.IsEnum() || t.IsTuple()) return "ptr";
        return LT(t, p.Sp);
    }

    std::string SigOf(FnDecl& fd, const std::string& nameOverride) {
        CurModule = fd.Module;
        std::string ret = fd.HasRet ? LT(TypeOf(fd.Ret), fd.Sp) : "void";
        std::string sig = "define internal " + ret + " @" +
                          (nameOverride.empty() ? Q(Key(fd.Module, fd.Name)) : nameOverride) + "(";
        for (size_t i = 0; i < fd.Params.size(); i++) {
            if (i) sig += ", ";
            sig += ParamLT(fd.Params[i]) + " %a" + std::to_string(i);
        }
        CurModule = "";
        return sig + ")";
    }

    void EmitForeignDecl(FnDecl& fd) {
        CurModule = fd.Module;
        Type rt = fd.HasRet ? TypeOf(fd.Ret) : Type::Void();
        std::string ret = rt.IsBool() ? "i32" : (rt.IsVoid() ? "void" : LT(rt, fd.Sp));
        std::string sig = "declare " + ret + " @" + fd.Name + "(";
        for (size_t i = 0; i < fd.Params.size(); i++) {
            if (i) sig += ", ";
            Type t = TypeOf(fd.Params[i].Ty);
            if (t.IsStr() || t.IsArray() || t.IsMap()) sig += "ptr";
            else if (t.IsBool()) sig += "i32";
            else sig += LT(t, fd.Params[i].Sp);
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
            bool agg = t.IsStruct() || t.IsEnum() || t.IsTuple();
            if (p.IsSelf || borrow) {
                VarPtr[pname] = preg;
            } else if (agg) {
                std::string lt = LT(t, p.Sp);
                std::string v = N();
                I(v + " = load " + lt + ", ptr " + preg);
                std::string lp = AllocaT(lt);
                I("store " + lt + " " + v + ", ptr " + lp);
                VarPtr[pname] = lp;
            } else {
                std::string lt = LT(t, p.Sp);
                std::string lp = AllocaT(lt);
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
        Bs << sig << " {\n" << Junta() << "}\n\n";
        RetType = Type::Err();
        CurModule = "";
    }

    void EmitMain() {
        if (!Opt.TestFns.empty()) {
            EmitTestMainLlvm();
            return;
        }
        auto f = FnByName.find("Main");
        if (f == FnByName.end()) return;
        Bs << "define i32 @main() {\n"
              "  call void @Main()\n"
              "  ret i32 0\n"
              "}\n\n";
    }

    void EmitTestMainLlvm() {
        Bs << "define i32 @main() {\n";
        Bs << "  call void @sy_test_arma()\n";
        for (size_t i = 0; i < Opt.TestFns.size(); i++) {
            const std::string& t = Opt.TestFns[i];
            std::string nb = StrConst(t);
            Bs << "  %r" << i << " = call i32 @setjmp(ptr @sylt_jb)\n";
            Bs << "  switch i32 %r" << i << ", label %fal" << i << " [\n";
            Bs << "    i32 0, label %run" << i << "\n";
            Bs << "    i32 1, label %fal" << i << "\n";
            Bs << "  ]\n";
            Bs << "run" << i << ":\n";
            Bs << "  call void @" << Key("", t) << "()\n";
            Bs << "  call void @sy_test_ok(ptr " << nb << ")\n";
            Bs << "  br label %next" << i << "\n";
            Bs << "fal" << i << ":\n";
            Bs << "  call void @sy_test_mal(ptr " << nb << ")\n";
            Bs << "  br label %next" << i << "\n";
            Bs << "next" << i << ":\n";
        }
        Bs << "  %f = call i64 @sy_test_resumen(i64 " << Opt.TestFns.size() << ")\n";
        Bs << "  %z = icmp sgt i64 %f, 0\n";
        Bs << "  %e = zext i1 %z to i32\n";
        Bs << "  ret i32 %e\n";
        Bs << "}\n\n";
    }

    void EmitStrOfProtos() {
        for (auto& td : Prog->Types) {
            if (!td->TypeParams.empty()) continue;
            EmitStrOfStruct(*td);
        }
        for (auto& ed : Prog->Enums) EmitStrOfEnum(*ed);
    }

    std::string StrOfField(const std::string& aggKey, const std::string& vreg, FieldDecl& f,
                           int idx) {
        Type ft = TypeOf(f.Ty);
        std::string fp = N();
        I(fp + " = getelementptr inbounds %" + Q(CppId(aggKey)) + ", ptr " + vreg + ", i32 0, i32 " +
          std::to_string(idx));
        if (ft.IsStr() || ft.IsArray() || ft.IsMap()) {
            std::string r = N();
            I(r + " = load ptr, ptr " + fp);
            return r;
        }
        if (ft.IsStruct() || ft.IsEnum()) {
            std::string r = N();
            I(r + " = call ptr @" + Q("sylt.str_of__" + CppId(ft.Name)) + "(ptr " + fp + ")");
            return r;
        }
        std::string lt = LT(ft, f.Sp);
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
        Bs << ("define private ptr @" + Q("sylt.str_of__" + CppId(key)) + "(ptr %a0) {\n")
           << Junta() << "}\n\n";
    }

    void EmitStrOfEnum(EnumDecl& ed) {
        std::string key = Key(ed.Module, ed.Name);
        Blocks.clear();
        Closed.clear();
        Undo.clear();
        VarPtr.clear();
        Open("");
        std::string tagp = N();
        I(tagp + " = getelementptr inbounds %" + Q(CppId(key)) + ", ptr %a0, i32 0, i32 0");
        std::string tag = N();
        I(tag + " = load i64, ptr " + tagp);
        std::string rp = AllocaT("ptr");
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
        (void)unk;
        Bs << ("define private ptr @" + Q("sylt.str_of__" + CppId(key)) + "(ptr %a0) {\n")
           << Junta() << "}\n\n";
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
    Out << "attributes #0 = { returns_twice }\n";
    Out << "attributes #1 = { noreturn }\n\n";
    Out << rt << "\n" << g.Bs.str();
    return Out.str();
}

}
