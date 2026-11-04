#include "gen.hpp"
#include <cstdio>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
namespace sylt {
namespace {
class Gen {
public:
    ProgramP& Prog;
    GenOptions Opt;
    std::ostringstream O;
    int Ind = 0;
    std::string CurModule;
    std::unordered_map<std::string, TypeDecl*> TypeByName;
    std::unordered_map<std::string, EnumDecl*> EnumByName;
    std::unordered_map<std::string, FnDecl*> FnByName;
    std::unordered_map<std::string, std::string> QualNames;
    explicit Gen(ProgramP& prog, const GenOptions& opt) : Prog(prog), Opt(opt) {
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
    void L(const std::string& s) {
        for (int i = 0; i < Ind; i++) O << "    ";
        O << s << "\n";
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
                if (a.Name.find('.') != std::string::npos) {
                    auto it = QualNames.find(a.Name);
                    return Type::StructOf(it != QualNames.end() ? it->second : a.Name);
                }
                if (!CurModule.empty()) {
                    std::string k = Key(CurModule, a.Name);
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
    std::string CppType(const Type& t) {
        switch (t.K) {
            case TK::Int: return "int64_t";
            case TK::Float: return "double";
            case TK::Bool: return "bool";
            case TK::Str: return "std::string";
            case TK::Void: return "void";
            case TK::Array: return "std::vector<" + CppType(t.Sub[0]) + ">";
            case TK::Map:
                return "std::unordered_map<" + CppType(t.Sub[0]) + ", " + CppType(t.Sub[1]) + ">";
            case TK::Tuple: {
                std::string s = "std::tuple<";
                for (size_t i = 0; i < t.Sub.size(); i++) {
                    if (i) s += ", ";
                    s += CppType(t.Sub[i]);
                }
                return s + ">";
            }
            case TK::Struct: return CppId(t.Name);
            case TK::Enum: return CppId(t.Name);
            case TK::Borrow: return CppType(t.Pointee());
            default: return "int64_t";
        }
    }
    std::string ParamSig(const Param& p) {
        Type t = TypeOf(p.Ty);
        if (p.Ty.k == TypeAnn::K::Borrow)
            return (p.Ty.Mut ? CppType(t) + "&" : "const " + CppType(t) + "&") + p.Name;
        return CppType(t) + " " + p.Name;
    }
    std::string EscapeCppString(const std::string& s) {
        std::string o;
        for (char c : s) {
            switch (c) {
                case '\\': o += "\\\\"; break;
                case '"': o += "\\\""; break;
                case '\n': o += "\\n"; break;
                case '\t': o += "\\t"; break;
                case '\r': o += "\\r"; break;
                default: o += c;
            }
        }
        return o;
    }
    std::string FloatLiteral(double v) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        std::string s(buf);
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
            s.find("inf") == std::string::npos && s.find("nan") == std::string::npos)
            s += ".0";
        return s;
    }
    std::string Cast(const std::string& code, const Type& from, const Type& to) {
        if (from.IsInt() && to.IsFloat()) return "(double)(" + code + ")";
        return code;
    }
    void Run() {
        O << "#include \"sylt_prelude.hpp\"\n";
        for (auto& inc : Prog->ForeignIncludes) L("#include " + inc);
        if (!Prog->ForeignIncludes.empty()) O << "\n";
        EmitSpawnTrampolines();
        EmitForwardDecls();
        EmitTypesInOrder();
        EmitStrOverloads();
        EmitFnProtos();
        EmitStructMethods();
        EmitFnBodies();
        EmitMain();
    }
    void WalkBlock(const Block& b) {
        for (auto& s : b.Stmts) WalkStmt(s.get());
    }
    void WalkStmt(Stmt* st) {
        switch (st->K) {
            case Stmt::Kind::Let: {
                auto* s = static_cast<LetStmt*>(st);
                WalkExpr(s->Init.get());
                return;
            }
            case Stmt::Kind::Assign: {
                auto* s = static_cast<AssignStmt*>(st);
                WalkExpr(s->Target.get());
                WalkExpr(s->Value.get());
                return;
            }
            case Stmt::Kind::Expr:
                WalkExpr(static_cast<ExprStmt*>(st)->Sub.get());
                return;
            case Stmt::Kind::Return: {
                auto* s = static_cast<ReturnStmt*>(st);
                if (s->Value) WalkExpr(s->Value.get());
                return;
            }
            case Stmt::Kind::If: {
                auto* s = static_cast<IfStmt*>(st);
                WalkExpr(s->Cond.get());
                WalkBlock(s->Then);
                for (auto& br : s->Elifs) {
                    WalkExpr(br.Cond.get());
                    WalkBlock(br.Body);
                }
                if (s->HasElse) WalkBlock(s->Else);
                return;
            }
            case Stmt::Kind::While: {
                auto* s = static_cast<WhileStmt*>(st);
                WalkExpr(s->Cond.get());
                WalkBlock(s->Body);
                return;
            }
            case Stmt::Kind::For: {
                auto* s = static_cast<ForStmt*>(st);
                WalkExpr(s->Low.get());
                if (s->IsRange && s->High) WalkExpr(s->High.get());
                WalkBlock(s->Body);
                return;
            }
            case Stmt::Kind::Match: {
                auto* s = static_cast<MatchStmt*>(st);
                WalkExpr(s->Subject.get());
                for (auto& arm : s->Arms) {
                    if (arm.Pat.Lit) WalkExpr(arm.Pat.Lit.get());
                    WalkBlock(arm.Body);
                }
                return;
            }
            default:
                return;
        }
    }
    void WalkExpr(Expr* e) {
        if (!e) return;
        switch (e->K) {
            case Expr::Kind::Call: {
                auto* c = static_cast<CallExpr*>(e);
                WalkExpr(c->Callee.get());
                for (auto& a : c->Args) WalkExpr(a.Value.get());
                if (c->Resolved == CallExpr::Target::Builtin &&
                    c->TargetName == "HiloLanzar" && !c->SpawnTarget.empty())
                    c->SpawnSite = SpawnSites++;
                return;
            }
            case Expr::Kind::Unary:
                WalkExpr(static_cast<UnaryExpr*>(e)->Sub.get());
                return;
            case Expr::Kind::Binary: {
                auto* b = static_cast<BinaryExpr*>(e);
                WalkExpr(b->L.get());
                WalkExpr(b->R.get());
                return;
            }
            case Expr::Kind::Index: {
                auto* ix = static_cast<IndexExpr*>(e);
                WalkExpr(ix->Base.get());
                WalkExpr(ix->Idx.get());
                return;
            }
            case Expr::Kind::Member:
                WalkExpr(static_cast<MemberExpr*>(e)->Base.get());
                return;
            case Expr::Kind::StrLit:
                for (auto& p : static_cast<StrLitExpr*>(e)->Parts)
                    if (!p.IsText) WalkExpr(p.Sub.get());
                return;
            case Expr::Kind::ArrayLit:
                for (auto& x : static_cast<ArrayLitExpr*>(e)->Elems) WalkExpr(x.get());
                return;
            case Expr::Kind::MapLit:
                for (auto& p : static_cast<MapLitExpr*>(e)->Pairs) {
                    WalkExpr(p.Key.get());
                    WalkExpr(p.Val.get());
                }
                return;
            case Expr::Kind::TupleLit:
                for (auto& x : static_cast<TupleLitExpr*>(e)->Elems) WalkExpr(x.get());
                return;
            default:
                return;
        }
    }
    int SpawnSites = 0;
    void EmitSpawnTrampolines() {
        for (auto& fd : Prog->Fns) {
            if (fd->Foreign) continue;
            WalkBlock(fd->Body);
        }
        for (auto& td : Prog->Types)
            for (auto& m : td->Methods) {
                if (!td->TypeParams.empty()) continue;
                WalkBlock(m->Body);
            }
        if (SpawnSites == 0) return;
        for (int site = 0; site < SpawnSites; site++) {
            Expr* hallado = nullptr;
            for (auto& fd : Prog->Fns) {
                if (fd->Foreign) continue;
                hallado = BuscaSite(fd->Body, site);
                if (hallado) break;
            }
            if (!hallado)
                for (auto& td : Prog->Types) {
                    if (!td->TypeParams.empty()) continue;
                    for (auto& m : td->Methods) {
                        hallado = BuscaSite(m->Body, site);
                        if (hallado) break;
                    }
                    if (hallado) break;
                }
            auto* c = static_cast<CallExpr*>(hallado);
            auto f = FnByName.find(c->SpawnTarget);
            std::vector<Param*> ps;
            if (f != FnByName.end())
                for (auto& p : f->second->Params)
                    if (!p.IsSelf) ps.push_back(&p);
            std::string ctx = "SyltCtx" + std::to_string(site);
            std::string tramp = "SyltTramp" + std::to_string(site);
            std::string campos, uses, tipos;
            for (size_t i = 0; i < ps.size(); i++) {
                Type pt = TypeOf(ps[i]->Ty);
                campos += (i ? " " : "") + CppType(pt) + " A" + std::to_string(i) + ";";
                uses += (i ? ", " : "") + std::string("C->A") + std::to_string(i);
                tipos += (i ? ", " : "") + CppType(pt);
            }
            if (f != FnByName.end()) {
                std::string ret = f->second->HasRet ? CppType(TypeOf(f->second->Ret)) : "void";
                L("static " + ret + " " + c->SpawnTarget + "(" + tipos + ");");
            }
            L("struct " + ctx + " {");
            Ind++;
            L(campos);
            Ind--;
            L("};");
            L("static void " + tramp + "(void* P) {");
            Ind++;
            L(ctx + "* C = (" + ctx + "*)P;");
            L(c->SpawnTarget + "(" + uses + ");");
            L("delete C;");
            Ind--;
            L("}");
        }
        L("");
    }
    Expr* BuscaSite(const Block& b, int site) {
        for (auto& s : b.Stmts) {
            Expr* r = BuscaSiteStmt(s.get(), site);
            if (r) return r;
        }
        return nullptr;
    }
    Expr* BuscaSiteStmt(Stmt* st, int site) {
        switch (st->K) {
            case Stmt::Kind::Let:
                return BuscaSiteExpr(static_cast<LetStmt*>(st)->Init.get(), site);
            case Stmt::Kind::Assign: {
                auto* s = static_cast<AssignStmt*>(st);
                Expr* r = BuscaSiteExpr(s->Target.get(), site);
                if (r) return r;
                return BuscaSiteExpr(s->Value.get(), site);
            }
            case Stmt::Kind::Expr:
                return BuscaSiteExpr(static_cast<ExprStmt*>(st)->Sub.get(), site);
            case Stmt::Kind::Return: {
                auto* s = static_cast<ReturnStmt*>(st);
                return s->Value ? BuscaSiteExpr(s->Value.get(), site) : nullptr;
            }
            case Stmt::Kind::If: {
                auto* s = static_cast<IfStmt*>(st);
                Expr* r = BuscaSiteExpr(s->Cond.get(), site);
                if (r) return r;
                r = BuscaSite(s->Then, site);
                if (r) return r;
                for (auto& br : s->Elifs) {
                    r = BuscaSiteExpr(br.Cond.get(), site);
                    if (r) return r;
                    r = BuscaSite(br.Body, site);
                    if (r) return r;
                }
                if (s->HasElse) return BuscaSite(s->Else, site);
                return nullptr;
            }
            case Stmt::Kind::While: {
                auto* s = static_cast<WhileStmt*>(st);
                Expr* r = BuscaSiteExpr(s->Cond.get(), site);
                if (r) return r;
                return BuscaSite(s->Body, site);
            }
            case Stmt::Kind::For: {
                auto* s = static_cast<ForStmt*>(st);
                Expr* r = BuscaSiteExpr(s->Low.get(), site);
                if (r) return r;
                if (s->IsRange && s->High) {
                    r = BuscaSiteExpr(s->High.get(), site);
                    if (r) return r;
                }
                return BuscaSite(s->Body, site);
            }
            case Stmt::Kind::Match: {
                auto* s = static_cast<MatchStmt*>(st);
                Expr* r = BuscaSiteExpr(s->Subject.get(), site);
                if (r) return r;
                for (auto& arm : s->Arms) {
                    if (arm.Pat.Lit) {
                        r = BuscaSiteExpr(arm.Pat.Lit.get(), site);
                        if (r) return r;
                    }
                    r = BuscaSite(arm.Body, site);
                    if (r) return r;
                }
                return nullptr;
            }
            default:
                return nullptr;
        }
    }
    Expr* BuscaSiteExpr(Expr* e, int site) {
        if (!e) return nullptr;
        if (e->K == Expr::Kind::Call) {
            auto* c = static_cast<CallExpr*>(e);
            if (c->Resolved == CallExpr::Target::Builtin &&
                c->TargetName == "HiloLanzar" && c->SpawnSite == site)
                return e;
            for (auto& a : c->Args) {
                Expr* r = BuscaSiteExpr(a.Value.get(), site);
                if (r) return r;
            }
            return nullptr;
        }
        switch (e->K) {
            case Expr::Kind::Unary:
                return BuscaSiteExpr(static_cast<UnaryExpr*>(e)->Sub.get(), site);
            case Expr::Kind::Binary: {
                auto* b = static_cast<BinaryExpr*>(e);
                Expr* r = BuscaSiteExpr(b->L.get(), site);
                if (r) return r;
                return BuscaSiteExpr(b->R.get(), site);
            }
            case Expr::Kind::Index: {
                auto* ix = static_cast<IndexExpr*>(e);
                Expr* r = BuscaSiteExpr(ix->Base.get(), site);
                if (r) return r;
                return BuscaSiteExpr(ix->Idx.get(), site);
            }
            case Expr::Kind::Member:
                return BuscaSiteExpr(static_cast<MemberExpr*>(e)->Base.get(), site);
            case Expr::Kind::StrLit:
                for (auto& p : static_cast<StrLitExpr*>(e)->Parts)
                    if (!p.IsText) {
                        Expr* r = BuscaSiteExpr(p.Sub.get(), site);
                        if (r) return r;
                    }
                return nullptr;
            case Expr::Kind::ArrayLit:
                for (auto& x : static_cast<ArrayLitExpr*>(e)->Elems) {
                    Expr* r = BuscaSiteExpr(x.get(), site);
                    if (r) return r;
                }
                return nullptr;
            case Expr::Kind::MapLit:
                for (auto& p : static_cast<MapLitExpr*>(e)->Pairs) {
                    Expr* r = BuscaSiteExpr(p.Key.get(), site);
                    if (r) return r;
                    r = BuscaSiteExpr(p.Val.get(), site);
                    if (r) return r;
                }
                return nullptr;
            case Expr::Kind::TupleLit:
                for (auto& x : static_cast<TupleLitExpr*>(e)->Elems) {
                    Expr* r = BuscaSiteExpr(x.get(), site);
                    if (r) return r;
                }
                return nullptr;
            default:
                return nullptr;
        }
    }
    void EmitForwardDecls() {
        for (auto& td : Prog->Types) {
            if (!td->TypeParams.empty()) continue;
            L("struct " + CppId(Key(td->Module, td->Name)) + ";");
        }
        if (!Prog->Types.empty()) L("");
    }
    struct DepNode {
        std::string Name;
        bool IsEnum = false;
        TypeDecl* TD = nullptr;
        EnumDecl* ED = nullptr;
        std::vector<std::string> Deps;
    };
    void EmitTypesInOrder() {
        std::unordered_map<std::string, DepNode> nodes;
        for (auto& td : Prog->Types) {
            if (!td->TypeParams.empty()) continue;
            DepNode n;
            n.Name = Key(td->Module, td->Name);
            n.TD = td.get();
            CurModule = td->Module;
            for (auto& f : td->Fields) {
                Type ft = TypeOf(f.Ty);
                if (ft.IsStruct()) n.Deps.push_back(ft.Name);
            }
            nodes[n.Name] = n;
            CurModule = "";
        }
        for (auto& ed : Prog->Enums) {
            DepNode n;
            n.Name = Key(ed->Module, ed->Name);
            n.IsEnum = true;
            n.ED = ed.get();
            CurModule = ed->Module;
            for (auto& v : ed->Variants) {
                for (auto& f : v.Fields) {
                    Type ft = TypeOf(f.Ty);
                    if (ft.IsStruct()) n.Deps.push_back(ft.Name);
                }
            }
            nodes[n.Name] = n;
            CurModule = "";
        }
        std::unordered_set<std::string> done;
        std::function<void(DepNode&)> visit = [&](DepNode& n) {
            if (done.count(n.Name)) return;
            done.insert(n.Name);
            for (auto& d : n.Deps) {
                auto it = nodes.find(d);
                if (it != nodes.end()) visit(it->second);
            }
            if (n.IsEnum) EmitEnum(*n.ED, n.Name);
            else EmitStruct(*n.TD, n.Name);
        };
        for (auto& kv : nodes) visit(kv.second);
        if (!nodes.empty()) L("");
    }
    void EmitStruct(TypeDecl& td, const std::string& key) {
        CurModule = td.Module;
        std::string cid = CppId(key);
        std::string s = "struct " + cid + " {";
        bool multiline = !td.Fields.empty() || !td.Methods.empty();
        if (multiline) { O << s << "\n"; Ind++; }
        else L(s);
        for (auto& f : td.Fields) {
            std::string line = "    " + CppType(TypeOf(f.Ty)) + " " + f.Name;
            if (f.HasDefault) line += " = " + GenExpr(f.Default.get());
            L(line + ";");
        }
        for (auto& m : td.Methods) EmitMethodDecl(*m);
        if (multiline) { Ind--; L("};"); }
        else L("};");
        CurModule = "";
    }
    void EmitEnum(EnumDecl& ed, const std::string& key) {
        CurModule = ed.Module;
        std::string cid = CppId(key);
        L("struct " + cid + " {");
        Ind++;
        L("int64_t SyltTag = 0;");
        std::vector<std::string> fieldOrder;
        std::unordered_map<std::string, std::string> fieldTypes;
        for (auto& v : ed.Variants) {
            for (auto& f : v.Fields) {
                if (fieldTypes.count(f.Name)) continue;
                fieldTypes[f.Name] = CppType(TypeOf(f.Ty));
                fieldOrder.push_back(f.Name);
            }
        }
        for (auto& fn : fieldOrder) L(fieldTypes[fn] + " f_" + fn + " = {};");
        if (!ed.Variants.empty()) L("");
        for (size_t vi = 0; vi < ed.Variants.size(); vi++) {
            EnumVariant& v = ed.Variants[vi];
            std::string sig = "static " + cid + " " + v.Name + "(";
            bool first = true;
            for (auto& f : v.Fields) {
                if (!first) sig += ", ";
                first = false;
                sig += CppType(TypeOf(f.Ty)) + " " + f.Name;
            }
            L(sig + ") {");
            Ind++;
            L(cid + " V;");
            L("V.SyltTag = " + std::to_string(vi) + ";");
            for (auto& f : v.Fields) L("V.f_" + f.Name + " = std::move(" + f.Name + ");");
            L("return V;");
            Ind--;
            L("}");
        }
        Ind--;
        L("};");
        L("");
        CurModule = "";
    }
    void EmitMethodDecl(const FnDecl& m) {
        bool selfMut = !m.Params.empty() && m.Params[0].IsSelf && m.Params[0].Mut;
        std::string ret = m.HasRet ? CppType(TypeOf(m.Ret)) : "void";
        std::string sig = "    " + ret + " " + m.Name + "(";
        bool first = true;
        for (auto& p : m.Params) {
            if (p.IsSelf) continue;
            if (!first) sig += ", ";
            first = false;
            sig += ParamSig(p);
        }
        sig += ")";
        if (!selfMut && !m.Params.empty() && m.Params[0].IsSelf) sig += " const";
        L(sig + ";");
    }
    void EmitStructMethods() {
        for (auto& td : Prog->Types) {
            if (!td->TypeParams.empty()) continue;
            std::string key = CppId(Key(td->Module, td->Name));
            CurModule = td->Module;
            for (auto& m : td->Methods) {
                bool selfMut = !m->Params.empty() && m->Params[0].IsSelf && m->Params[0].Mut;
                std::string ret = m->HasRet ? CppType(TypeOf(m->Ret)) : "void";
                std::string sig = ret + " " + key + "::" + m->Name + "(";
                bool first = true;
                for (auto& p : m->Params) {
                    if (p.IsSelf) continue;
                    if (!first) sig += ", ";
                    first = false;
                    sig += ParamSig(p);
                }
                sig += ")";
                if (!selfMut && !m->Params.empty() && m->Params[0].IsSelf) sig += " const";
                Type retTy = m->HasRet ? TypeOf(m->Ret) : Type::Void();
                CurrentRetType = m->HasRet ? &retTy : nullptr;
                L(sig + " {");
                Ind++;
                L(selfMut ? "auto& self = *this;" : "const auto& self = *this;");
                GenBlockInner(m->Body);
                Ind--;
                L("}");
                L("");
                CurrentRetType = nullptr;
            }
            CurModule = "";
        }
    }
    void EmitStrOverloads() {
        for (auto& td : Prog->Types) {
            if (!td->TypeParams.empty()) continue;
            std::string key = CppId(Key(td->Module, td->Name));
            CurModule = td->Module;
            std::string expr =
                "std::string(\"" + EscapeCppString(td->Name + "{") + "\")";
            for (size_t i = 0; i < td->Fields.size(); i++) {
                std::string sep = (i == 0) ? "" : ", ";
                expr += " + \"" + EscapeCppString(sep + td->Fields[i].Name + ": ") + "\"";
                expr += " + SyltStr(V." + td->Fields[i].Name + ")";
            }
            expr += " + \"}\"";
            L("inline std::string SyltStr(const " + key + "& V) {");
            Ind++;
            L("return " + expr + ";");
            Ind--;
            L("}");
            L("");
            CurModule = "";
        }
        for (auto& ed : Prog->Enums) {
            std::string key = CppId(Key(ed->Module, ed->Name));
            L("inline std::string SyltStr(const " + key + "& V) {");
            Ind++;
            for (size_t vi = 0; vi < ed->Variants.size(); vi++) {
                EnumVariant& v = ed->Variants[vi];
                std::string head = (vi == 0 ? "" : "} else ") +
                    std::string("if (V.SyltTag == ") + std::to_string(vi) + ") {";
                if (v.Fields.empty()) {
                    L(head);
                    Ind++;
                    L("return std::string(\"" + EscapeCppString(v.Name) + "\");");
                    Ind--;
                } else {
                    L(head);
                    Ind++;
                    std::string expr =
                        "std::string(\"" + EscapeCppString(v.Name + "{") + "\")";
                    for (size_t i = 0; i < v.Fields.size(); i++) {
                        std::string sep = (i == 0) ? "" : ", ";
                        expr += " + \"" + EscapeCppString(sep + v.Fields[i].Name + ": ") + "\"";
                        expr += " + SyltStr(V.f_" + v.Fields[i].Name + ")";
                    }
                    expr += " + \"}\"";
                    L("return " + expr + ";");
                    Ind--;
                }
            }
            if (!ed->Variants.empty()) {
                L("} else {");
                Ind++;
                L("return std::string(\"" + EscapeCppString(ed->Name + "{?}") + "\");");
                Ind--;
                L("}");
            }
            Ind--;
            L("}");
            L("");
        }
    }
    void EmitFnProtos() {
        for (auto& fd : Prog->Fns) {
            if (fd->Foreign) continue;
            std::string key = Key(fd->Module, fd->Name);
            CurModule = fd->Module;
            std::string ret = fd->HasRet ? CppType(TypeOf(fd->Ret)) : "void";
            std::string sig = "static " + ret + " " + key + "(";
            bool first = true;
            for (auto& p : fd->Params) {
                if (!first) sig += ", ";
                first = false;
                sig += ParamSig(p);
            }
            sig += ")";
            L(sig + ";");
            CurModule = "";
        }
        if (!Prog->Fns.empty()) L("");
    }
    void EmitFnBodies() {
        for (auto& fd : Prog->Fns) {
            if (fd->Foreign) continue;
            std::string key = Key(fd->Module, fd->Name);
            CurModule = fd->Module;
            std::string ret = fd->HasRet ? CppType(TypeOf(fd->Ret)) : "void";
            std::string sig = "static " + ret + " " + key + "(";
            bool first = true;
            for (auto& p : fd->Params) {
                if (!first) sig += ", ";
                first = false;
                sig += ParamSig(p);
            }
            sig += ")";
            Type retTy = fd->HasRet ? TypeOf(fd->Ret) : Type::Void();
            CurrentRetType = fd->HasRet ? &retTy : nullptr;
            L(sig + " {");
            Ind++;
            GenBlockInner(fd->Body);
            Ind--;
            L("}");
            L("");
            CurrentRetType = nullptr;
            CurModule = "";
        }
    }
    void EmitMain() {
        if (!Opt.TestFns.empty()) {
            EmitTestMain();
            return;
        }
        auto f = FnByName.find("Main");
        if (f == FnByName.end()) return;
        L("static void SyltUserMain() {");
        Ind++;
        L("Main();");
        Ind--;
        L("}");
        L("");
        L("int main() {");
        Ind++;
        L("SyltUserMain();");
        L("return 0;");
        Ind--;
        L("}");
    }
    void EmitTestMain() {
        L("int main() {");
        Ind++;
        L("int SyltFallos = 0;");
        L("(void)SyltFallos;");
        for (auto& t : Opt.TestFns) {
            L("{");
            Ind++;
            L("try {");
            Ind++;
            L(t + "();");
            L("std::cout << \"ok " + t + "\" << std::endl;");
            Ind--;
            L("} catch (const SyltPruebaFallo& e) {");
            Ind++;
            L("++SyltFallos;");
            L("std::cout << \"fallo: " + t +
              " (línea \" << e.Linea << \"): \" << e.Mensaje << std::endl;");
            Ind--;
            L("}");
            Ind--;
            L("}");
        }
        L("std::cout << " + std::to_string(Opt.TestFns.size()) +
          " << \" pruebas, \" << SyltFallos");
        L("          << (SyltFallos == 1 ? \" fallo\" : \" fallos\") << std::endl;");
        L("return SyltFallos > 0 ? 1 : 0;");
        Ind--;
        L("}");
    }
    void GenBlockInner(const Block& b) {
        for (auto& s : b.Stmts) GenStmt(s.get());
    }
    void GenStmt(Stmt* st) {
        switch (st->K) {
            case Stmt::Kind::Let: {
                auto* s = static_cast<LetStmt*>(st);
                std::string init = GenExpr(s->Init.get());
                init = Cast(init, s->Init->Ty, s->VarType);
                if (s->Destructure) {
                    std::string names;
                    for (size_t i = 0; i < s->Names.size(); i++) {
                        if (i) names += ", ";
                        names += s->Names[i];
                    }
                    L("auto [" + names + "] = " + init + ";");
                    return;
                }
                const std::string& name = s->Names[0];
                if (name == "_") { L("(void)(" + init + ");"); return; }
                Type vt = s->VarType;
                std::string cty;
                if (vt.IsInt()) cty = "int64_t";
                else if (vt.IsFloat()) cty = "double";
                else if (vt.IsBool()) cty = "bool";
                else if (vt.IsStr()) cty = "std::string";
                else cty = "auto";
                L(cty + " " + name + " = " + init + ";");
                return;
            }
            case Stmt::Kind::Assign: {
                auto* s = static_cast<AssignStmt*>(st);
                std::string value = GenExpr(s->Value.get());
                if (s->ValMoved) value = "std::move(" + value + ")";
                Expr* t = s->Target.get();
                if (t->K == Expr::Kind::Ident) {
                    auto* id = static_cast<IdentExpr*>(t);
                    std::string v = Cast(value, s->Value->Ty, id->Ty);
                    L(id->Name + " " + s->Op + " " + v + ";");
                    return;
                }
                if (t->K == Expr::Kind::Index) {
                    auto* ix = static_cast<IndexExpr*>(t);
                    std::string base = GenExpr(ix->Base.get());
                    std::string idx = GenExpr(ix->Idx.get());
                    if (ix->TupleIndex) {
                        L("std::get<" + std::to_string(ix->TuplePos) + ">(" + base +
                          ") " + s->Op + " " + value + ";");
                        return;
                    }
                    Type bt = ix->Base->Ty.Pointee();
                    if (bt.IsMap()) {
                        L("(" + base + ")[" + idx + "] = " + value + ";");
                        return;
                    }
                    if (Opt.Fast) {
                        L("(" + base + ")[(size_t)(" + idx + ")] " + s->Op + " " + value + ";");
                    } else {
                        L("SyltIdx(" + base + ", " + idx + ", \"" +
                          EscapeCppString(BaseName(ix->Base.get())) + "\") " + s->Op + " " +
                          value + ";");
                    }
                    return;
                }
                if (t->K == Expr::Kind::Member) {
                    auto* m = static_cast<MemberExpr*>(t);
                    std::string base = GenExpr(m->Base.get());
                    L("(" + base + ")." + m->Name + " " + s->Op + " " + value + ";");
                    return;
                }
                return;
            }
            case Stmt::Kind::Expr:
                L(GenExpr(static_cast<ExprStmt*>(st)->Sub.get()) + ";");
                return;
            case Stmt::Kind::Return: {
                auto* s = static_cast<ReturnStmt*>(st);
                if (!s->Value) { L("return;"); return; }
                std::string v = GenExpr(s->Value.get());
                if (CurrentRetType)
                    v = Cast(v, s->Value->Ty, *CurrentRetType);
                L("return " + v + ";");
                return;
            }
            case Stmt::Kind::If: {
                auto* s = static_cast<IfStmt*>(st);
                std::string head = "if (" + GenExpr(s->Cond.get()) + ") {";
                L(head);
                Ind++; GenBlockInner(s->Then); Ind--;
                for (auto& br : s->Elifs) {
                    L("} else if (" + GenExpr(br.Cond.get()) + ") {");
                    Ind++; GenBlockInner(br.Body); Ind--;
                }
                if (s->HasElse) {
                    L("} else {");
                    Ind++; GenBlockInner(s->Else); Ind--;
                }
                L("}");
                return;
            }
            case Stmt::Kind::While: {
                auto* s = static_cast<WhileStmt*>(st);
                L("while (" + GenExpr(s->Cond.get()) + ") {");
                Ind++; GenBlockInner(s->Body); Ind--;
                L("}");
                return;
            }
            case Stmt::Kind::For: {
                auto* s = static_cast<ForStmt*>(st);
                if (s->IsRange) {
                    L("for (int64_t " + s->Var + " = " + GenExpr(s->Low.get()) + "; " +
                      s->Var + " < " + GenExpr(s->High.get()) + "; ++" + s->Var + ") {");
                    Ind++; GenBlockInner(s->Body); Ind--;
                    L("}");
                    return;
                }
                Type it = s->Low->Ty;
                if (it.IsMap()) {
                    L("{");
                    Ind++;
                    L("const auto SyltKeys = SyltClaves(" + GenExpr(s->Low.get()) + ");");
                    L("for (const auto " + s->Var + " : SyltKeys) {");
                    Ind++; GenBlockInner(s->Body); Ind--;
                    L("}");
                    Ind--;
                    L("}");
                    return;
                }
                L("{");
                Ind++;
                L("const auto& SyltContainer = " + GenExpr(s->Low.get()) + ";");
                L("for (const auto " + s->Var + " : SyltContainer) {");
                Ind++; GenBlockInner(s->Body); Ind--;
                L("}");
                Ind--;
                L("}");
                return;
            }
            case Stmt::Kind::Match: {
                auto* s = static_cast<MatchStmt*>(st);
                L("{");
                Ind++;
                L("const auto& SyltSubject = " + GenExpr(s->Subject.get()) + ";");
                bool open = false;
                for (auto& arm : s->Arms) {
                    if (arm.Pat.k == Pattern::K::Wildcard) {
                        L(open ? "} else {" : "{");
                        open = true;
                        Ind++;
                        GenBlockInner(arm.Body);
                        Ind--;
                        continue;
                    }
                    std::string cond;
                    if (arm.Pat.k == Pattern::K::Literal) {
                        cond = "(SyltSubject == " + GenExpr(arm.Pat.Lit.get()) + ")";
                    } else {
                        cond = "(SyltSubject.SyltTag == " + std::to_string(arm.Pat.TagIndex) + ")";
                    }
                    L(std::string(open ? "} else if (" : "if (") + cond + ") {");
                    open = true;
                    Ind++;
                    if (arm.Pat.k == Pattern::K::Variant) {
                        for (size_t i = 0; i < arm.Pat.Bindings.size() &&
                                          i < arm.Pat.FieldNames.size(); i++) {
                            if (arm.Pat.Bindings[i] == "_") continue;
                            L("const auto& " + arm.Pat.Bindings[i] +
                              " = SyltSubject.f_" + arm.Pat.FieldNames[i] + ";");
                        }
                    }
                    GenBlockInner(arm.Body);
                    Ind--;
                }
                if (open) L("}");
                Ind--;
                L("}");
                return;
            }
            case Stmt::Kind::Break:
                L("break;");
                return;
            case Stmt::Kind::Continue:
                L("continue;");
                return;
            case Stmt::Kind::Pass:
                L(";");
                return;
        }
    }
    const Type* CurrentRetType = nullptr;
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
    std::string GenExpr(Expr* e) {
        switch (e->K) {
            case Expr::Kind::IntLit:
                return "(int64_t)" + std::to_string(static_cast<IntLitExpr*>(e)->Val) + "LL";
            case Expr::Kind::FloatLit:
                return FloatLiteral(static_cast<FloatLitExpr*>(e)->Val);
            case Expr::Kind::BoolLit:
                return static_cast<BoolLitExpr*>(e)->Val ? "true" : "false";
            case Expr::Kind::StrLit: {
                auto* s = static_cast<StrLitExpr*>(e);
                std::string out;
                bool first = true;
                for (auto& p : s->Parts) {
                    std::string piece;
                    if (p.IsText) piece = "\"" + EscapeCppString(p.Text) + "\"";
                    else piece = "SyltStr(" + GenExpr(p.Sub.get()) + ")";
                    if (first) out = piece;
                    else out += " + " + piece;
                    first = false;
                }
                if (first) out = "\"\"";
                if (s->HasInterp) out = "std::string(" + out + ")";
                return out;
            }
            case Expr::Kind::Ident:
                return static_cast<IdentExpr*>(e)->Name;
            case Expr::Kind::Unary: {
                auto* u = static_cast<UnaryExpr*>(e);
                if (u->Op == "-") return "(-" + GenExpr(u->Sub.get()) + ")";
                return "(!(" + GenExpr(u->Sub.get()) + "))";
            }
            case Expr::Kind::Binary: {
                auto* b = static_cast<BinaryExpr*>(e);
                std::string l = GenExpr(b->L.get());
                std::string r = GenExpr(b->R.get());
                const std::string& op = b->Op;
                if (op == "+" && b->Ty.IsStr()) {
                    if (!l.empty() && l[0] == '"') l = "std::string(" + l + ")";
                    if (!r.empty() && r[0] == '"') r = "std::string(" + r + ")";
                }
                if (op == "and") return "(" + l + " && " + r + ")";
                if (op == "or") return "(" + l + " || " + r + ")";
                if (!Opt.Fast && b->Ty.IsInt()) {
                    if (op == "/") return "SyltDivI(" + l + ", " + r + ")";
                    if (op == "%") return "SyltModI(" + l + ", " + r + ")";
                }
                return "(" + l + " " + op + " " + r + ")";
            }
            case Expr::Kind::Call:
                return GenCall(static_cast<CallExpr*>(e));
            case Expr::Kind::Index: {
                auto* ix = static_cast<IndexExpr*>(e);
                std::string base = GenExpr(ix->Base.get());
                if (ix->TupleIndex)
                    return "std::get<" + std::to_string(ix->TuplePos) + ">(" + base + ")";
                std::string idx = GenExpr(ix->Idx.get());
                Type bt = ix->Base->Ty.Pointee();
                if (bt.IsMap())
                    return "SyltMGet(" + base + ", " + idx + ", \"" +
                           EscapeCppString(BaseName(ix->Base.get())) + "\")";
                if (Opt.Fast)
                    return "(" + base + ")[(size_t)(" + idx + ")]";
                return "SyltIdx(" + base + ", " + idx + ", \"" +
                       EscapeCppString(BaseName(ix->Base.get())) + "\")";
            }
            case Expr::Kind::Member: {
                auto* m = static_cast<MemberExpr*>(e);
                if (m->IsEnumUnit)
                    return m->EnumKey + "::" + m->Name + "()";
                return "(" + GenExpr(m->Base.get()) + ")." + m->Name;
            }
            case Expr::Kind::ArrayLit: {
                auto* a = static_cast<ArrayLitExpr*>(e);
                std::string elem = a->Ty.IsArray() ? CppType(a->Ty.Sub[0]) : "int64_t";
                std::string s = "std::vector<" + elem + ">{";
                bool first = true;
                for (size_t i = 0; i < a->Elems.size(); i++) {
                    if (!first) s += ", ";
                    first = false;
                    std::string code = GenExpr(a->Elems[i].get());
                    if (i < a->ElemsMoved.size() && a->ElemsMoved[i])
                        code = "std::move(" + code + ")";
                    s += Cast(code, a->Elems[i]->Ty, a->Ty.Sub[0]);
                }
                return s + "}";
            }
            case Expr::Kind::MapLit: {
                auto* m = static_cast<MapLitExpr*>(e);
                std::string k = m->Ty.IsMap() ? CppType(m->Ty.Sub[0]) : "std::string";
                std::string v = m->Ty.IsMap() ? CppType(m->Ty.Sub[1]) : "int64_t";
                std::string s = "std::unordered_map<" + k + ", " + v + ">{";
                bool first = true;
                for (size_t i = 0; i < m->Pairs.size(); i++) {
                    if (!first) s += ", ";
                    first = false;
                    std::string vcode = GenExpr(m->Pairs[i].Val.get());
                    if (m->Pairs[i].ValMoved) vcode = "std::move(" + vcode + ")";
                    s += "{" + Cast(GenExpr(m->Pairs[i].Key.get()), m->Pairs[i].Key->Ty,
                                     m->Ty.Sub[0]) + ", " +
                         Cast(vcode, m->Pairs[i].Val->Ty, m->Ty.Sub[1]) + "}";
                }
                return s + "}";
            }
            case Expr::Kind::TupleLit: {
                auto* t = static_cast<TupleLitExpr*>(e);
                std::string s = "std::make_tuple(";
                bool first = true;
                for (size_t ei = 0; ei < t->Elems.size(); ei++) {
                    if (!first) s += ", ";
                    first = false;
                    std::string code = GenExpr(t->Elems[ei].get());
                    if (ei < t->Ty.Sub.size() && t->Ty.Sub[ei].IsStr())
                        code = "std::string(" + code + ")";
                    s += code;
                }
                return s + ")";
            }
        }
        return "0";
    }
    std::string ArgCode(CallExpr* c, size_t i) {
        std::string code = GenExpr(c->Args[i].Value.get());
        if (i < c->ArgCastFloat.size() && c->ArgCastFloat[i])
            return "(double)(" + code + ")";
        return code;
    }
    std::string MovedArg(CallExpr* c, size_t i) {
        std::string code = ArgCode(c, i);
        if (i < c->ArgMoved.size() && c->ArgMoved[i])
            code = "std::move(" + code + ")";
        return code;
    }
    std::string GenCall(CallExpr* c) {
        switch (c->Resolved) {
            case CallExpr::Target::Builtin: {
                const std::string& n = c->TargetName;
                if (n == "TiempoMs") return "SyltTiempoMs()";
                if (n == "Print") return "SyltPrint(" + ArgCode(c, 0) + ")";
                if (n == "Str") {
                    if (c->Args.size() == 2)
                        return "SyltStrPrec(" + ArgCode(c, 0) + ", " + ArgCode(c, 1) + ")";
                    return "SyltStr(" + ArgCode(c, 0) + ")";
                }
                if (n == "Sqrt") return "SyltSqrt(" + ArgCode(c, 0) + ")";
                if (n == "Abs") return "SyltAbs(" + ArgCode(c, 0) + ")";
                if (n == "Floor") return "SyltFloor(" + ArgCode(c, 0) + ")";
                if (n == "Clone") return "SyltClone(" + ArgCode(c, 0) + ")";
                if (n == "Min") return "SyltMin(" + ArgCode(c, 0) + ", " + ArgCode(c, 1) + ")";
                if (n == "Max") return "SyltMax(" + ArgCode(c, 0) + ", " + ArgCode(c, 1) + ")";
                if (n == "Pow") return "SyltPow(" + ArgCode(c, 0) + ", " + ArgCode(c, 1) + ")";
                if (n == "Afirma")
                    return "SyAfirma(" + ArgCode(c, 0) + ", " + ArgCode(c, 1) + ", " +
                           std::to_string(c->Sp.Line) + ")";
                if (n == "HiloLanzar") {
                    std::string S = "sy_hilo_lanzar(&SyltTramp" +
                                    std::to_string(c->SpawnSite) + ", new SyltCtx" +
                                    std::to_string(c->SpawnSite) + "{";
                    for (size_t i = 1; i < c->Args.size(); i++) {
                        if (i > 1) S += ", ";
                        S += ArgCode(c, i);
                    }
                    return S + "})";
                }
                return "0";
            }
            case CallExpr::Target::Fn: {
                std::string s = c->TargetName + "(";
                for (size_t i = 0; i < c->Args.size(); i++) {
                    if (i) s += ", ";
                    s += MovedArg(c, i);
                }
                return s + ")";
            }
            case CallExpr::Target::EnumCtor: {
                std::string s = c->TargetName + "::" + c->VariantName + "(";
                for (size_t i = 0; i < c->Args.size(); i++) {
                    if (i) s += ", ";
                    s += MovedArg(c, i);
                }
                return s + ")";
            }
            case CallExpr::Target::StructInit: {
                auto it = TypeByName.find(c->TargetName);
                if (it == TypeByName.end()) return CppId(c->TargetName) + "{}";
                TypeDecl* td = it->second;
                bool named = !c->Args.empty() && !c->Args[0].Name.empty();
                std::vector<std::string> vals(td->Fields.size(), "");
                CurModule = td->Module;
                if (named) {
                    for (auto& a : c->Args) {
                        for (size_t i = 0; i < td->Fields.size(); i++) {
                            if (td->Fields[i].Name == a.Name) {
                                vals[i] = Cast(GenExpr(a.Value.get()), a.Value->Ty,
                                               TypeOf(td->Fields[i].Ty));
                            }
                        }
                    }
                } else {
                    for (size_t i = 0; i < c->Args.size() && i < td->Fields.size(); i++) {
                        vals[i] = Cast(GenExpr(c->Args[i].Value.get()), c->Args[i].Value->Ty,
                                       TypeOf(td->Fields[i].Ty));
                    }
                }
                CurModule = "";
                for (size_t i = 0; i < vals.size(); i++) {
                    if (vals[i].empty() && td->Fields[i].HasDefault && td->Fields[i].Default) {
                        CurModule = td->Module;
                        vals[i] = GenExpr(td->Fields[i].Default.get());
                        CurModule = "";
                    }
                }
                std::string s = CppId(c->TargetName) + "{";
                bool first = true;
                for (auto& v : vals) {
                    if (!first) s += ", ";
                    first = false;
                    s += v;
                }
                return s + "}";
            }
            case CallExpr::Target::Method: {
                auto* m = static_cast<MemberExpr*>(c->Callee.get());
                std::string base = GenExpr(m->Base.get());
                const std::string& n = c->TargetName;
                {
                    Type bt = m->Base->Ty.Pointee();
                    if (bt.IsArray() || bt.IsMap() || bt.IsStr()) {
                        if (n == "Push")
                            return "SyltPush(" + base + ", " + MovedArg(c, 0) + ")";
                        if (n == "Pop") return "SyltPop(" + base + ")";
                        if (n == "Len") return "((int64_t)(" + base + ").size())";
                        if (n == "Claves") return "SyltClaves(" + base + ")";
                        if (n == "Contiene") {
                            if (bt.IsMap())
                                return "(SyltMGetCount(" + base + ", " + ArgCode(c, 0) + ") > 0)";
                            if (bt.IsStr())
                                return "SyltContieneStr(" + base + ", " + ArgCode(c, 0) + ")";
                            return "SyltContieneVec(" + base + ", " + ArgCode(c, 0) + ")";
                        }
                    }
                }
                std::string s = "(" + base + ")." + n + "(";
                for (size_t i = 0; i < c->Args.size(); i++) {
                    if (i) s += ", ";
                    s += MovedArg(c, i);
                }
                return s + ")";
            }
            default:
                return "0";
        }
    }
};
}
std::string GenCpp(ProgramP& prog, const GenOptions& opt) {
    Gen g(prog, opt);
    g.Run();
    return g.O.str();
}
}
