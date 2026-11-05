#include "check.hpp"
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "lex.hpp"
namespace sylt {
namespace {
static std::string DisplayName(const std::string& n) {
    std::string o;
    for (size_t i = 0; i < n.size(); i++) {
        if (i + 1 < n.size() && n[i] == '_' && n[i + 1] == '_') {
            o += '.';
            i++;
        } else {
            o += n[i];
        }
    }
    return o;
}
struct FieldInfo {
    std::string Name;
    Type Ty;
    bool HasDefault = false;
    Span Sp;
};
struct StructInfo {
    std::string Name;
    std::vector<FieldInfo> Fields;
    std::unordered_map<std::string, FnDecl*> Methods;
    Span Sp;
};
struct EnumVarInfo {
    std::string Name;
    int Index = 0;
    std::vector<FieldInfo> Fields;
    Span Sp;
};
struct EnumInfo {
    std::string Name;
    std::vector<EnumVarInfo> Variants;
    Span Sp;
};
struct VariantInfo {
    std::string EnumName;
    std::string VariantName;
    int Index = 0;
    Span Sp;
};
TypeAnn CloneTypeAnn(const TypeAnn& a) {
    TypeAnn o;
    o.k = a.k;
    o.Name = a.Name;
    o.Mut = a.Mut;
    o.Sp = a.Sp;
    o.HasResolvedTy = a.HasResolvedTy;
    o.ResolvedTy = a.ResolvedTy;
    for (auto& s : a.Sub) o.Sub.push_back(CloneTypeAnn(s));
    return o;
}
ExprP CloneExpr(const Expr* e);
Block CloneBlock(const Block& b);
StmtP CloneStmt(const Stmt* s) {
    switch (s->K) {
        case Stmt::Kind::Let: {
            auto* p = static_cast<const LetStmt*>(s);
            auto o = std::make_unique<LetStmt>();
            o->Mutable = p->Mutable;
            o->Names = p->Names;
            o->Destructure = p->Destructure;
            o->HasTy = p->HasTy;
            o->Ty = CloneTypeAnn(p->Ty);
            if (p->Init) o->Init = CloneExpr(p->Init.get());
            o->Sp = p->Sp;
            return o;
        }
        case Stmt::Kind::Assign: {
            auto* p = static_cast<const AssignStmt*>(s);
            auto o = std::make_unique<AssignStmt>();
            o->Op = p->Op;
            o->Target = CloneExpr(p->Target.get());
            o->Value = CloneExpr(p->Value.get());
            o->Sp = p->Sp;
            return o;
        }
        case Stmt::Kind::Expr: {
            auto* p = static_cast<const ExprStmt*>(s);
            auto o = std::make_unique<ExprStmt>();
            o->Sub = CloneExpr(p->Sub.get());
            o->Sp = p->Sp;
            return o;
        }
        case Stmt::Kind::Return: {
            auto* p = static_cast<const ReturnStmt*>(s);
            auto o = std::make_unique<ReturnStmt>();
            if (p->Value) o->Value = CloneExpr(p->Value.get());
            o->Sp = p->Sp;
            return o;
        }
        case Stmt::Kind::If: {
            auto* p = static_cast<const IfStmt*>(s);
            auto o = std::make_unique<IfStmt>();
            o->Cond = CloneExpr(p->Cond.get());
            o->Then = CloneBlock(p->Then);
            for (auto& br : p->Elifs) {
                IfStmt::Branch nb;
                nb.Cond = CloneExpr(br.Cond.get());
                nb.Body = CloneBlock(br.Body);
                o->Elifs.push_back(std::move(nb));
            }
            o->HasElse = p->HasElse;
            o->Else = CloneBlock(p->Else);
            o->Sp = p->Sp;
            return o;
        }
        case Stmt::Kind::While: {
            auto* p = static_cast<const WhileStmt*>(s);
            auto o = std::make_unique<WhileStmt>();
            o->Cond = CloneExpr(p->Cond.get());
            o->Body = CloneBlock(p->Body);
            o->Sp = p->Sp;
            return o;
        }
        case Stmt::Kind::For: {
            auto* p = static_cast<const ForStmt*>(s);
            auto o = std::make_unique<ForStmt>();
            o->Var = p->Var;
            o->IsRange = p->IsRange;
            if (p->Low) o->Low = CloneExpr(p->Low.get());
            if (p->High) o->High = CloneExpr(p->High.get());
            o->Body = CloneBlock(p->Body);
            o->Sp = p->Sp;
            return o;
        }
        case Stmt::Kind::Match: {
            auto* p = static_cast<const MatchStmt*>(s);
            auto o = std::make_unique<MatchStmt>();
            o->Subject = CloneExpr(p->Subject.get());
            for (auto& arm : p->Arms) {
                MatchArm na;
                na.Pat.k = arm.Pat.k;
                na.Pat.Name = arm.Pat.Name;
                na.Pat.Qualified = arm.Pat.Qualified;
                na.Pat.Bindings = arm.Pat.Bindings;
                if (arm.Pat.Lit) na.Pat.Lit = CloneExpr(arm.Pat.Lit.get());
                na.Pat.Sp = arm.Pat.Sp;
                na.Body = CloneBlock(arm.Body);
                o->Arms.push_back(std::move(na));
            }
            o->Sp = p->Sp;
            return o;
        }
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
        case Stmt::Kind::Pass: {
            auto o = std::make_unique<Stmt>(s->K);
            o->Sp = s->Sp;
            return o;
        }
    }
    auto o = std::make_unique<Stmt>(s->K);
    o->Sp = s->Sp;
    return o;
}
Block CloneBlock(const Block& b) {
    Block o;
    o.Sp = b.Sp;
    for (auto& s : b.Stmts) o.Stmts.push_back(CloneStmt(s.get()));
    return o;
}
ExprP CloneExpr(const Expr* e) {
    switch (e->K) {
        case Expr::Kind::IntLit: {
            auto* p = static_cast<const IntLitExpr*>(e);
            auto o = std::make_unique<IntLitExpr>();
            o->Val = p->Val;
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::FloatLit: {
            auto* p = static_cast<const FloatLitExpr*>(e);
            auto o = std::make_unique<FloatLitExpr>();
            o->Val = p->Val;
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::BoolLit: {
            auto* p = static_cast<const BoolLitExpr*>(e);
            auto o = std::make_unique<BoolLitExpr>();
            o->Val = p->Val;
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::StrLit: {
            auto* p = static_cast<const StrLitExpr*>(e);
            auto o = std::make_unique<StrLitExpr>();
            for (auto& part : p->Parts) {
                StrPart np;
                np.IsText = part.IsText;
                np.Text = part.Text;
                if (part.Sub) np.Sub = CloneExpr(part.Sub.get());
                o->Parts.push_back(std::move(np));
            }
            o->HasInterp = p->HasInterp;
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::Ident: {
            auto* p = static_cast<const IdentExpr*>(e);
            auto o = std::make_unique<IdentExpr>();
            o->Name = p->Name;
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::Unary: {
            auto* p = static_cast<const UnaryExpr*>(e);
            auto o = std::make_unique<UnaryExpr>();
            o->Op = p->Op;
            o->Sub = CloneExpr(p->Sub.get());
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::Binary: {
            auto* p = static_cast<const BinaryExpr*>(e);
            auto o = std::make_unique<BinaryExpr>();
            o->Op = p->Op;
            o->L = CloneExpr(p->L.get());
            o->R = CloneExpr(p->R.get());
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::Call: {
            auto* p = static_cast<const CallExpr*>(e);
            auto o = std::make_unique<CallExpr>();
            o->Callee = CloneExpr(p->Callee.get());
            for (auto& a : p->Args) {
                Arg na;
                na.Name = a.Name;
                na.Borrowed = a.Borrowed;
                na.Value = CloneExpr(a.Value.get());
                na.Sp = a.Sp;
                o->Args.push_back(std::move(na));
            }
            for (auto& ta : p->TypeArgs) o->TypeArgs.push_back(CloneTypeAnn(ta));
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::Index: {
            auto* p = static_cast<const IndexExpr*>(e);
            auto o = std::make_unique<IndexExpr>();
            o->Base = CloneExpr(p->Base.get());
            o->Idx = CloneExpr(p->Idx.get());
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::Member: {
            auto* p = static_cast<const MemberExpr*>(e);
            auto o = std::make_unique<MemberExpr>();
            o->Name = p->Name;
            o->NameSp = p->NameSp;
            o->Base = CloneExpr(p->Base.get());
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::ArrayLit: {
            auto* p = static_cast<const ArrayLitExpr*>(e);
            auto o = std::make_unique<ArrayLitExpr>();
            for (auto& el : p->Elems) o->Elems.push_back(CloneExpr(el.get()));
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::MapLit: {
            auto* p = static_cast<const MapLitExpr*>(e);
            auto o = std::make_unique<MapLitExpr>();
            for (auto& pr : p->Pairs) {
                MapPair np;
                np.Key = CloneExpr(pr.Key.get());
                np.Val = CloneExpr(pr.Val.get());
                np.Sp = pr.Sp;
                o->Pairs.push_back(std::move(np));
            }
            o->Sp = p->Sp;
            return o;
        }
        case Expr::Kind::TupleLit: {
            auto* p = static_cast<const TupleLitExpr*>(e);
            auto o = std::make_unique<TupleLitExpr>();
            for (auto& el : p->Elems) o->Elems.push_back(CloneExpr(el.get()));
            o->Sp = p->Sp;
            return o;
        }
    }
    auto o = std::make_unique<IntLitExpr>();
    o->Sp = e->Sp;
    return o;
}
std::unique_ptr<FnDecl> CloneFn(const FnDecl* fd) {
    auto o = std::make_unique<FnDecl>();
    o->Name = fd->Name;
    for (auto& p : fd->Params) {
        Param np;
        np.Name = p.Name;
        np.IsSelf = p.IsSelf;
        np.Mut = p.Mut;
        np.HasTy = p.HasTy;
        np.Ty = CloneTypeAnn(p.Ty);
        np.Sp = p.Sp;
        o->Params.push_back(std::move(np));
    }
    o->HasRet = fd->HasRet;
    o->Ret = CloneTypeAnn(fd->Ret);
    o->Body = CloneBlock(fd->Body);
    o->Sp = fd->Sp;
    o->IsMethod = fd->IsMethod;
    o->SelfMut = fd->SelfMut;
    o->OwnerStruct = fd->OwnerStruct;
    o->Foreign = false;
    o->Module = fd->Module;
    return o;
}
std::unique_ptr<TypeDecl> CloneTypeDecl(const TypeDecl* td) {
    auto o = std::make_unique<TypeDecl>();
    o->Name = td->Name;
    o->TypeParams = td->TypeParams;
    for (auto& f : td->Fields) {
        FieldDecl nf;
        nf.Name = f.Name;
        nf.Ty = CloneTypeAnn(f.Ty);
        nf.HasDefault = f.HasDefault;
        if (f.Default) nf.Default = CloneExpr(f.Default.get());
        nf.Sp = f.Sp;
        o->Fields.push_back(std::move(nf));
    }
    for (auto& m : td->Methods) o->Methods.push_back(CloneFn(m.get()));
    o->Sp = td->Sp;
    o->Module = td->Module;
    return o;
}
struct ModuleInfo {
    std::string Name;
    std::unordered_map<std::string, std::string> Fns;
    std::unordered_map<std::string, std::string> Types;
    std::unordered_map<std::string, std::string> Enums;
    Span Sp;
};
void SubstTypeAnn(TypeAnn& a, const std::vector<std::string>& params,
                  const std::vector<Type>& args) {
    switch (a.k) {
        case TypeAnn::K::Ident:
            for (size_t i = 0; i < params.size(); i++) {
                if (a.Name == params[i]) {
                    a.HasResolvedTy = true;
                    a.ResolvedTy = args[i];
                    return;
                }
            }
            return;
        case TypeAnn::K::Array:
            if (!a.Sub.empty()) SubstTypeAnn(a.Sub[0], params, args);
            return;
        case TypeAnn::K::Map:
            if (a.Sub.size() > 0) SubstTypeAnn(a.Sub[0], params, args);
            if (a.Sub.size() > 1) SubstTypeAnn(a.Sub[1], params, args);
            return;
        case TypeAnn::K::Tuple:
        case TypeAnn::K::Generic:
        case TypeAnn::K::Borrow:
            for (auto& s : a.Sub) SubstTypeAnn(s, params, args);
            return;
        default:
            return;
    }
}
void SubstStmt(Stmt* st, const std::vector<std::string>& params,
               const std::vector<Type>& args);
void SubstBlock(Block& b, const std::vector<std::string>& params,
                const std::vector<Type>& args) {
    for (auto& s : b.Stmts) SubstStmt(s.get(), params, args);
}
void SubstStmt(Stmt* st, const std::vector<std::string>& params,
               const std::vector<Type>& args) {
    switch (st->K) {
        case Stmt::Kind::Let: {
            auto* s = static_cast<LetStmt*>(st);
            if (s->HasTy) SubstTypeAnn(s->Ty, params, args);
            return;
        }
        case Stmt::Kind::If: {
            auto* s = static_cast<IfStmt*>(st);
            SubstBlock(s->Then, params, args);
            for (auto& br : s->Elifs) SubstBlock(br.Body, params, args);
            if (s->HasElse) SubstBlock(s->Else, params, args);
            return;
        }
        case Stmt::Kind::While:
            SubstBlock(static_cast<WhileStmt*>(st)->Body, params, args);
            return;
        case Stmt::Kind::For:
            SubstBlock(static_cast<ForStmt*>(st)->Body, params, args);
            return;
        case Stmt::Kind::Match: {
            auto* s = static_cast<MatchStmt*>(st);
            for (auto& arm : s->Arms) SubstBlock(arm.Body, params, args);
            return;
        }
        default:
            return;
    }
}

struct GenericInfo {
    TypeDecl* TD = nullptr;
    std::string Key;
    std::string Module;
};
struct VarInfo {
    Type Ty;
    bool Mutable = false;
    bool Moveable = false;
    bool Moved = false;
    bool Used = false;
    bool IsBorrowParam = false;
    Span DeclSp, MoveSp;
    enum class Kind { Let, Var, Param, LoopVar, Self } KindV = Kind::Let;
};
struct BuiltinDef {
    const char* Name;
    int MinArgs, MaxArgs;
};
const std::vector<BuiltinDef> kBuiltinNames = {
    {"Print", 1, 1}, {"Str", 1, 2},   {"Sqrt", 1, 1}, {"Abs", 1, 1},
    {"Min", 2, 2},   {"Max", 2, 2},   {"Pow", 2, 2},  {"Floor", 1, 1},
    {"Clone", 1, 1}, {"TiempoMs", 0, 0}, {"Afirma", 2, 2}, {"HiloLanzar", 1, 9},
};
bool IsBuiltin(const std::string& n) {
    for (auto& b : kBuiltinNames)
        if (n == b.Name) return true;
    return false;
}
class Checker {
public:
    const SourceFile& Src;
    Reporter& Rep;
    ProgramP& Prog;
    std::unordered_map<std::string, StructInfo> Structs;
    std::unordered_map<std::string, EnumInfo> Enums;
    std::unordered_map<std::string, VariantInfo> Variantes;
    std::unordered_map<std::string, ModuleInfo> Mods;
    std::unordered_map<std::string, GenericInfo> Generics;
    std::vector<std::unique_ptr<TypeDecl>> InstDecls;
    std::vector<std::pair<std::string, FnDecl*>> PendingCheck;
    bool CheckingInstantiation = false;
    std::unordered_map<std::string, FnDecl*> Fns;
    std::vector<std::unordered_map<std::string, VarInfo>> Scopes;
    FnDecl* CurFn = nullptr;
    Type CurRet;
    bool CurRetIsVoid = false;
    int InLoop = 0;
    bool Failed = false;
    std::string CurModule;
    const std::unordered_map<std::string, const SourceFile*>* Files = nullptr;
    Checker(const SourceFile& src, Reporter& rep, ProgramP& prog)
        : Src(src), Rep(rep), Prog(prog) {}
    void UseModuleFile(const std::string& mod) {
        if (!Files) return;
        auto f = Files->find(mod);
        Rep.Use(f != Files->end() ? f->second : &Src);
    }
    bool HadError() const { return Rep.ErrorCount() > 0; }
    static bool IsErr(const Type& t) { return t.IsError(); }
    static std::string Mangle(const std::string& mod, const std::string& n) {
        if (mod.empty()) return n;
        std::string m = mod;
        for (auto& c : m)
            if (c == '.') c = '_';
        return m + "__" + n;
    }
    static std::string FnKey(const FnDecl* fd) {
        return Mangle(fd->Module, fd->Name);
    }
    FnDecl* FindFn(const std::string& n) {
        if (!CurModule.empty()) {
            auto f = Fns.find(Mangle(CurModule, n));
            if (f != Fns.end()) return f->second;
        }
        auto f = Fns.find(n);
        return f != Fns.end() ? f->second : nullptr;
    }
    StructInfo* FindStruct(const std::string& n) {
        if (!CurModule.empty()) {
            auto f = Structs.find(Mangle(CurModule, n));
            if (f != Structs.end()) return &f->second;
        }
        auto f = Structs.find(n);
        return f != Structs.end() ? &f->second : nullptr;
    }
    EnumInfo* FindEnum(const std::string& n) {
        if (!CurModule.empty()) {
            auto f = Enums.find(Mangle(CurModule, n));
            if (f != Enums.end()) return &f->second;
        }
        auto f = Enums.find(n);
        return f != Enums.end() ? &f->second : nullptr;
    }
    std::string FindGenericKey(const std::string& n) {
        if (!CurModule.empty()) {
            auto f = Generics.find(Mangle(CurModule, n));
            if (f != Generics.end()) return f->second.Key;
        }
        auto f2 = Generics.find(n);
        if (f2 != Generics.end()) return f2->second.Key;
        return "";
    }
    const EnumVarInfo* FindVariantIn(const EnumInfo& ei, const std::string& vn) {
        for (auto& v : ei.Variants)
            if (v.Name == vn) return &v;
        return nullptr;
    }
    VariantInfo* FindVariant(const std::string& n) {
        auto f = Variantes.find(n);
        return f != Variantes.end() ? &f->second : nullptr;
    }
    std::string ModulePathOf(Expr* e) {
        std::vector<std::string> Partes;
        Expr* cur = e;
        while (cur->K == Expr::Kind::Member) {
            Partes.push_back(static_cast<MemberExpr*>(cur)->Name);
            cur = static_cast<MemberExpr*>(cur)->Base.get();
        }
        if (cur->K != Expr::Kind::Ident) return "";
        Partes.push_back(static_cast<IdentExpr*>(cur)->Name);
        std::string path;
        for (size_t i = Partes.size(); i-- > 0;)
            path += (path.empty() ? "" : ".") + Partes[i];
        return Mods.count(path) ? path : "";
    }
    std::string SuggestName(const std::string& n, Span* outSp) {
        std::string best;
        int bestD = 1 << 30;
        Span sp;
        auto consider = [&](const std::string& cand, const Span& s) {
            if (!CloseEnough(n, cand)) return;
            int d = Levenshtein(n, cand);
            if (d < bestD) { bestD = d; best = cand; sp = s; }
        };
        for (auto& sc : Scopes)
            for (auto& kv : sc) consider(kv.first, kv.second.DeclSp);
        for (auto& kv : Fns) {
            if (!kv.second->Module.empty()) continue;
            consider(kv.first, kv.second->Sp);
        }
        for (auto& kv : Structs) consider(kv.first, kv.second.Sp);
        for (auto& kv : Enums) consider(kv.first, kv.second.Sp);
        for (auto& kv : Variantes) consider(kv.first, kv.second.Sp);
        for (auto& mo : Mods)
            for (auto& f : mo.second.Fns) consider(f.first, mo.second.Sp);
        for (auto& b : kBuiltinNames) consider(b.Name, Span());
        if (best.empty()) return "";
        if (outSp) *outSp = sp;
        return best;
    }
    Diag UnknownName(const std::string& code, Span sp, const std::string& kind,
                     const std::string& n) {
        Diag d = Rep.Err(code, sp, kind + " `" + n + "` no existe en este programa");
        Span ssp;
        std::string s = SuggestName(n, &ssp);
        if (!s.empty()) {
            d.Helps.push_back("¿quisiste decir `" + s + "`?");
        }
        return d;
    }
    void Push() { Scopes.emplace_back(); }
    void Pop() { Scopes.pop_back(); }
    VarInfo* FindVar(const std::string& n) {
        for (auto it = Scopes.rbegin(); it != Scopes.rend(); ++it) {
            auto f = it->find(n);
            if (f != it->end()) return &f->second;
        }
        return nullptr;
    }
    void Bind(const std::string& n, VarInfo v, Span sp) {
        auto& top = Scopes.back();
        auto f = top.find(n);
        if (f != top.end()) {
            Diag d = Rep.Err("E0305", sp, "`" + n + "` ya está declarado en este bloque");
            d.Notes.push_back({Diag::NoteKind::Note, "lo declaraste antes aquí", f->second.DeclSp});
            d.Helps.push_back("si necesitas otro valor, usa otro nombre o reasigna con `=`");
            Rep.Emit(d);
            return;
        }
        top[n] = std::move(v);
    }
    using MoveSnap = std::vector<std::unordered_map<std::string, VarInfo>>;
    void MergeMovesInto(MoveSnap& base, size_t depth) {
        for (size_t d = 0; d < depth && d < Scopes.size() && d < base.size(); d++) {
            for (auto& kv : Scopes[d]) {
                auto f = base[d].find(kv.first);
                if (f == base[d].end()) continue;
                if (kv.second.Moved && !f->second.Moved) {
                    f->second.Moved = true;
                    f->second.MoveSp = kv.second.MoveSp;
                }
                if (kv.second.Used) f->second.Used = true;
            }
        }
    }
    bool SameMoves(const MoveSnap& A, const MoveSnap& B, size_t depth) {
        for (size_t d = 0; d < depth && d < A.size() && d < B.size(); d++) {
            if (A[d].size() != B[d].size()) return false;
            for (auto& kv : A[d]) {
                auto f = B[d].find(kv.first);
                if (f == B[d].end()) return false;
                if (f->second.Moved != kv.second.Moved) return false;
                if (f->second.Used != kv.second.Used) return false;
                if (f->second.Moved && (f->second.MoveSp.Line != kv.second.MoveSp.Line ||
                                        f->second.MoveSp.Col != kv.second.MoveSp.Col))
                    return false;
            }
        }
        return true;
    }
    void WalkBranch(const Block& b, MoveSnap& acum) {
        MoveSnap saved = Scopes;
        size_t depth = Scopes.size();
        Push();
        WalkBlock(b);
        Pop();
        MergeMovesInto(acum, depth);
        Scopes = std::move(saved);
    }
    void ReportNotMutable(Span sp, const std::string& name, const VarInfo& v) {
        Diag d;
        switch (v.KindV) {
            case VarInfo::Kind::Let:
                d = Rep.Err("E0504", sp,
                    "no puedes modificar `" + name + "` porque está declarado con `let`");
                d.Helps.push_back("decláralo con `var` si necesitas cambiarlo: `var " + name + " = ...`");
                break;
            case VarInfo::Kind::Param:
                if (v.IsBorrowParam) {
                    d = Rep.Err("E0504", sp,
                        "no puedes modificar a través del préstamo inmutable `" + name + "`");
                    d.Helps.push_back("pide el parámetro como `&mut` en la firma de la función");
                } else {
                    d = Rep.Err("E0504", sp, "el parámetro `" + name + "` es inmutable");
                    d.Helps.push_back("decláralo mutable en la firma: `mut " + name + "`");
                }
                break;
            case VarInfo::Kind::LoopVar:
                d = Rep.Err("E0504", sp,
                    "la variable del bucle `" + name + "` es inmutable");
                d.Helps.push_back("si necesitas un contador propio, usa `var I = 0` fuera del bucle");
                break;
            case VarInfo::Kind::Self:
                d = Rep.Err("E0504", sp,
                    "no puedes modificar `self` en un método con `self` (sin `mut`)");
                d.Helps.push_back("cambia la firma del método a `mut self`");
                break;
            default:
                d = Rep.Err("E0504", sp, "no puedes modificar `" + name + "`");
                break;
        }
        d.Notes.push_back({Diag::NoteKind::Note, "declarado aquí", v.DeclSp});
        Rep.Emit(d);
    }
    Expr* RootExpr(Expr* e) {
        while (true) {
            if (e->K == Expr::Kind::Member)
                e = static_cast<MemberExpr*>(e)->Base.get();
            else if (e->K == Expr::Kind::Index)
                e = static_cast<IndexExpr*>(e)->Base.get();
            else
                return e;
        }
    }
    bool CheckMutableTarget(Expr* e) {
        Expr* root = RootExpr(e);
        if (root->K != Expr::Kind::Ident) return true;
        auto* id = static_cast<IdentExpr*>(root);
        VarInfo* v = FindVar(id->Name);
        if (!v) return true;
        if (!v->Mutable) {
            ReportNotMutable(root->Sp, id->Name, *v);
            return false;
        }
        return true;
    }
    bool UseVar(Expr* e) {
        if (e->K != Expr::Kind::Ident) return true;
        auto* id = static_cast<IdentExpr*>(e);
        VarInfo* v = FindVar(id->Name);
        if (!v) return true;
        v->Used = true;
        if (v->Moved) {
            Diag d = Rep.Err("E0501", e->Sp,
                "no puedes usar `" + id->Name + "` porque su valor ya se movió");
            d.Notes.push_back({Diag::NoteKind::Note,
                "el valor se movió en esta otra línea", v->MoveSp});
            d.Helps.push_back("los structs no se copian: se mueven. Si quieres conservar "
                              "el original, escribe `Clone(" + id->Name +
                              ")` en el punto de movimiento");
            Rep.Emit(d);
            return false;
        }
        return true;
    }
    bool MoveVar(Expr* e) {
        if (e->K != Expr::Kind::Ident) return false;
        auto* id = static_cast<IdentExpr*>(e);
        VarInfo* v = FindVar(id->Name);
        if (!v || !v->Moveable || v->Moved) return false;
        v->Moved = true;
        v->MoveSp = e->Sp;
        return true;
    }
    TypeAnn TypeAnnOf(const Type& t) {
        TypeAnn a;
        switch (t.K) {
            case TK::Int: a.k = TypeAnn::K::Prim; a.Name = "int"; return a;
            case TK::Float: a.k = TypeAnn::K::Prim; a.Name = "float"; return a;
            case TK::Bool: a.k = TypeAnn::K::Prim; a.Name = "bool"; return a;
            case TK::Str: a.k = TypeAnn::K::Prim; a.Name = "string"; return a;
            case TK::Array:
                a.k = TypeAnn::K::Array;
                a.Sub.push_back(TypeAnnOf(t.Sub[0]));
                return a;
            case TK::Map:
                a.k = TypeAnn::K::Map;
                a.Sub.push_back(TypeAnnOf(t.Sub[0]));
                a.Sub.push_back(TypeAnnOf(t.Sub[1]));
                return a;
            case TK::Tuple:
                a.k = TypeAnn::K::Tuple;
                for (auto& s : t.Sub) a.Sub.push_back(TypeAnnOf(s));
                return a;
            case TK::Struct:
            case TK::Enum:
                a.k = TypeAnn::K::Ident;
                a.Name = t.Name;
                return a;
            default:
                a.k = TypeAnn::K::Prim;
                a.Name = "int";
                return a;
        }
    }
    void CollapseGenerics(TypeAnn& a) {
        if (a.HasResolvedTy) return;
        if (a.k == TypeAnn::K::Generic) {
            Type t = ResolveType(a, false, false);
            if (!t.IsError()) {
                a.HasResolvedTy = true;
                a.ResolvedTy = t;
            }
            return;
        }
        for (auto& s : a.Sub) CollapseGenerics(s);
    }
    std::string InstantiateGeneric(const std::string& genKey, const std::vector<Type>& args,
                                   Span sp) {
        auto g = Generics.find(genKey);
        if (g == Generics.end()) return "";
        GenericInfo gi = g->second;
        const std::vector<std::string>& params = gi.TD->TypeParams;
        if (args.size() != params.size()) {
            std::string plist;
            for (auto& p : params) plist += (plist.empty() ? "" : ", ") + p;
            Diag d = Rep.Err("E0311", sp,
                "`" + DisplayName(genKey) + "` espera " + std::to_string(params.size()) +
                (params.size() == 1 ? " argumento de tipo" : " argumentos de tipo") +
                " (" + plist + "), pero recibe " + std::to_string(args.size()));
            d.Helps.push_back("ejemplo: " + DisplayName(genKey) + "[" +
                              (params.empty() ? std::string("int")
                                              : TypeName(args.empty() ? Type::Int() : args[0])) +
                              "]");
            Rep.Emit(d);
            return "";
        }
        std::string pretty;
        for (size_t i = 0; i < args.size(); i++) {
            if (i) pretty += ",";
            pretty += TypeName(args[i]);
        }
        std::string key = genKey + "[" + pretty + "]";
        if (Structs.count(key)) return key;
        for (auto& at : args) {
            if (at.IsBorrow() || at.IsVoid()) {
                Diag d = Rep.Err("E0312", sp,
                    "el argumento de tipo `" + TypeName(at) + "` no es válido aquí");
                d.Helps.push_back("usa un tipo completo: int, string, un struct tuyo...");
                Rep.Emit(d);
                return "";
            }
        }
        auto inst = CloneTypeDecl(gi.TD);
        inst->Name = key;
        inst->Module = "";
        inst->TypeParams.clear();
        for (auto& f : inst->Fields) SubstTypeAnn(f.Ty, params, args);
        for (auto& m : inst->Methods) {
            for (auto& p : m->Params)
                if (p.HasTy) SubstTypeAnn(p.Ty, params, args);
            if (m->HasRet) SubstTypeAnn(m->Ret, params, args);
            SubstBlock(m->Body, params, args);
        }
        StructInfo si;
        si.Name = key;
        si.Sp = gi.TD->Sp;
        Structs[key] = std::move(si);
        std::unordered_set<std::string> seenFields;
        std::string savedMod = CurModule;
        CurModule = gi.Module;
        for (auto& f : inst->Fields) {
            CollapseGenerics(f.Ty);
            if (getenv("SYLT_DEBUG")) fprintf(stderr, "[dbg] field %s k=%d resolved=%d tyk=%d name=%s\n", f.Name.c_str(), (int)f.Ty.k, (int)f.Ty.HasResolvedTy, (int)f.Ty.ResolvedTy.K, f.Ty.Name.c_str());
        }
        for (auto& m : inst->Methods) {
            for (auto& p : m->Params)
                if (p.HasTy) CollapseGenerics(p.Ty);
            if (m->HasRet) CollapseGenerics(m->Ret);
        }
        for (auto& f : inst->Fields) {
            if (seenFields.count(f.Name)) {
                Diag d = Rep.Err("E0305", f.Sp,
                    "el campo `" + f.Name + "` está repetido en `" + DisplayName(key) + "`");
                Rep.Emit(d);
                continue;
            }
            seenFields.insert(f.Name);
            FieldInfo fi;
            fi.Name = f.Name;
            fi.Ty = ResolveType(f.Ty, false, false);
            fi.HasDefault = f.HasDefault;
            fi.Sp = f.Sp;
            if (f.HasDefault && f.Default) {
                Push();
                CheckExpr(f.Default.get(), IsErr(fi.Ty) ? nullptr : &fi.Ty);
                Pop();
            }
            Structs[key].Fields.push_back(std::move(fi));
        }
        for (auto& m : inst->Methods) {
            if (ValidateMethodSig(*m)) continue;
            if (Structs[key].Methods.count(m->Name)) {
                Diag d = Rep.Err("E0305", m->Sp,
                    "el método `" + m->Name + "` está repetido en `" + DisplayName(key) + "`");
                Rep.Emit(d);
                continue;
            }
            Structs[key].Methods[m->Name] = m.get();
            PendingCheck.push_back({key, m.get()});
        }
        CurModule = savedMod;
        InstDecls.push_back(std::move(inst));
        return key;
    }
    Type ResolveType(const TypeAnn& a, bool allowBorrow, bool allowVoid) {
        if (a.HasResolvedTy) return a.ResolvedTy;
        switch (a.k) {
            case TypeAnn::K::None:
                return Type::Err();
            case TypeAnn::K::Prim: {
                if (a.Name == "int") return Type::Int();
                if (a.Name == "float") return Type::Float();
                if (a.Name == "bool") return Type::Bool();
                if (a.Name == "string") return Type::Str();
                if (a.Name == "void") {
                    if (!allowVoid)
                        Rep.Emit(Rep.Err("E0408", a.Sp,
                            "`void` solo puede ser el tipo de retorno de una función"));
                    return Type::Void();
                }
                return Type::Err();
            }
            case TypeAnn::K::Ident: {
                if (a.Name.find('[') != std::string::npos) {
                    auto fs = Structs.find(a.Name);
                    if (fs != Structs.end()) return Type::StructOf(a.Name);
                    auto fe = Enums.find(a.Name);
                    if (fe != Enums.end()) return Type::EnumOf(a.Name);
                    Rep.Emit(UnknownName("E0303", a.Sp, "el tipo", a.Name));
                    return Type::Err();
                }
                if (a.Name.find('.') != std::string::npos) {
                    size_t dot = a.Name.find('.');
                    std::string mod = a.Name.substr(0, dot);
                    std::string local = a.Name.substr(dot + 1);
                    auto mo = Mods.find(mod);
                    if (mo != Mods.end()) {
                        auto st = mo->second.Types.find(local);
                        if (st != mo->second.Types.end()) return Type::StructOf(st->second);
                        auto en = mo->second.Enums.find(local);
                        if (en != mo->second.Enums.end()) return Type::EnumOf(en->second);
                    }
                    Diag d = UnknownName("E0303", a.Sp, "el tipo", a.Name);
                    d.Helps.push_back("los tipos cualificados se escriben `modulo.Tipo` "
                                      "tras importar el módulo");
                    Rep.Emit(d);
                    return Type::Err();
                }
                std::string key = CurModule.empty() ? a.Name : Mangle(CurModule, a.Name);
                auto f = Structs.find(key);
                if (f != Structs.end()) return Type::StructOf(key);
                auto fe = Enums.find(key);
                if (fe != Enums.end()) return Type::EnumOf(key);
                if (!Generics.empty()) {
                    std::string gk = FindGenericKey(a.Name);
                    if (!gk.empty()) {
                        Diag d = Rep.Err("E0310", a.Sp,
                            "el struct `" + a.Name + "` es genérico: indica sus tipos entre corchetes");
                        d.Helps.push_back("ejemplo: " + a.Name + "[int]");
                        Rep.Emit(d);
                        return Type::Err();
                    }
                }
                Diag d = UnknownName("E0303", a.Sp, "el tipo", a.Name);
                d.Helps.push_back("los tipos básicos son int, float, bool y string; "
                                  "los tuyos se definen con `type Nombre` o `enum Nombre`");
                Rep.Emit(d);
                return Type::Err();
            }
            case TypeAnn::K::Generic: {
                std::vector<Type> args;
                bool bad = false;
                for (auto& s : a.Sub) {
                    Type t = ResolveType(s, false, false);
                    args.push_back(t);
                    bad |= IsErr(t);
                }
                if (bad) return Type::Err();
                std::string gk;
                if (a.Name.find('.') != std::string::npos) {
                    size_t dot = a.Name.find('.');
                    auto mo = Mods.find(a.Name.substr(0, dot));
                    if (mo != Mods.end()) {
                        auto st = mo->second.Types.find(a.Name.substr(dot + 1));
                        if (st != mo->second.Types.end()) {
                            if (Generics.count(st->second)) gk = st->second;
                            else {
                                Diag d = Rep.Err("E0313", a.Sp,
                                    "`" + a.Name + "` no es un struct genérico: no lleva argumentos de tipo");
                                d.Helps.push_back("escribe `" + a.Name + "` sin corchetes");
                                Rep.Emit(d);
                                return Type::Err();
                            }
                        }
                    }
                    if (gk.empty()) {
                        Diag d = UnknownName("E0303", a.Sp, "el tipo", a.Name);
                        d.Helps.push_back("los tipos cualificados se escriben `modulo.Tipo` "
                                          "tras importar el módulo");
                        Rep.Emit(d);
                        return Type::Err();
                    }
                } else {
                    gk = FindGenericKey(a.Name);
                    if (gk.empty()) {
                        if (FindStruct(a.Name) || FindEnum(a.Name)) {
                            Diag d = Rep.Err("E0313", a.Sp,
                                "`" + a.Name + "` no es un struct genérico: no lleva argumentos de tipo");
                            d.Helps.push_back("escribe `" + a.Name + "` sin corchetes, o revisa el nombre");
                            Rep.Emit(d);
                        } else {
                            Diag d = UnknownName("E0303", a.Sp, "el tipo", a.Name);
                            d.Helps.push_back("si es genérico, decláralo con `type " + a.Name + "[T]:`");
                            Rep.Emit(d);
                        }
                        return Type::Err();
                    }
                }
                std::string key = InstantiateGeneric(gk, args, a.Sp);
                if (key.empty()) return Type::Err();
                return Type::StructOf(key);
            }
            case TypeAnn::K::Array:
                return Type::ArrayOf(ResolveType(a.Sub[0], false, false));
            case TypeAnn::K::Map: {
                Type k = ResolveType(a.Sub[0], false, false);
                Type v = ResolveType(a.Sub[1], false, false);
                if (k.IsError()) return Type::Err();
                if (!k.IsStr() && !k.IsInt()) {
                    Diag d = Rep.Err("E0401", a.Sub[0].Sp,
                        "las claves de un mapa deben ser `string` o `int`, pero son `" +
                        TypeName(k) + "`");
                    d.Helps.push_back("para otras claves, usa un array de structs con el campo clave");
                    Rep.Emit(d);
                    return Type::Err();
                }
                return Type::MapOf(k, v);
            }
            case TypeAnn::K::Tuple: {
                std::vector<Type> ms;
                for (auto& s : a.Sub) ms.push_back(ResolveType(s, false, false));
                return Type::TupleOf(std::move(ms));
            }
            case TypeAnn::K::Borrow: {
                if (!allowBorrow) {
                    Rep.Emit(Rep.Err("E0408", a.Sp,
                        "los préstamos `&` solo se permiten en parámetros de funciones (por ahora)"));
                    return Type::Err();
                }
                Type inner = ResolveType(a.Sub.empty() ? TypeAnn() : a.Sub[0], false, false);
                return Type::BorrowOf(inner, a.Mut);
            }
        }
        return Type::Err();
    }
    static bool AssignableTo(const Type& have, const Type& want) {
        if (IsErr(have) || IsErr(want)) return true;
        if (have.SameAs(want)) return true;
        if (have.IsInt() && want.IsFloat()) return true;
        return false;
    }
    void TypeMismatch(Span sp, const Type& want, const Type& have) {
        Diag d = Rep.Err("E0401", sp,
            "se esperaba `" + TypeName(want) + "`, pero encontré `" + TypeName(have) + "`");
        d.Helps.push_back("Sylt comprueba los tipos al compilar para que el binario "
                          "vuele sin comprobaciones en caliente");
        if (have.IsStr() && want.IsInt())
            d.Helps.push_back("si el número está dentro del texto, usa interpolación: \"{Texto}\"");
        Rep.Emit(d);
    }
    void RegisterAll() {
        for (auto& td : Prog->Types) {
            UseModuleFile(td->Module);
            std::string key = Mangle(td->Module, td->Name);
            if (Structs.count(key) || Fns.count(key) || Enums.count(key) ||
                Generics.count(key) || IsBuiltin(td->Name)) {
                Diag d = Rep.Err("E0305", td->Sp, "`" + key + "` ya existe");
                d.Helps.push_back("cada `type` y cada `fn` necesitan un nombre único");
                Rep.Emit(d);
                continue;
            }
            if (!td->TypeParams.empty()) {
                GenericInfo gi;
                gi.TD = td.get();
                gi.Key = key;
                gi.Module = td->Module;
                Generics[key] = gi;
                continue;
            }
            StructInfo si;
            si.Name = key;
            si.Sp = td->Sp;
            Structs[key] = std::move(si);
        }
        for (auto& ed : Prog->Enums) {
            UseModuleFile(ed->Module);
            std::string key = Mangle(ed->Module, ed->Name);
            if (Structs.count(key) || Fns.count(key) || Enums.count(key) ||
                Generics.count(key) || IsBuiltin(ed->Name)) {
                Diag d = Rep.Err("E0305", ed->Sp, "`" + key + "` ya existe");
                d.Helps.push_back("cada `type`, `enum` y `fn` necesita un nombre único");
                Rep.Emit(d);
                continue;
            }
            EnumInfo ei;
            ei.Name = key;
            ei.Sp = ed->Sp;
            Enums[key] = std::move(ei);
        }
        for (auto& ed : Prog->Enums) {
            UseModuleFile(ed->Module);
            std::string key = Mangle(ed->Module, ed->Name);
            if (!Enums.count(key)) continue;
            EnumInfo& ei = Enums[key];
            std::unordered_map<std::string, Type> sharedFields;
            for (auto& v : ed->Variants) {
                if (Variantes.count(v.Name)) {
                    const VariantInfo& prev = Variantes[v.Name];
                    Diag d = Rep.Err("E0305", v.Sp,
                        "la variante `" + v.Name + "` ya existe en el enum `" +
                        DisplayName(prev.EnumName) + "`");
                    d.Notes.push_back({Diag::NoteKind::Note, "variante anterior aquí", prev.Sp});
                    d.Helps.push_back("los nombres de variante son únicos en todo el programa");
                    Rep.Emit(d);
                    continue;
                }
                EnumVarInfo evi;
                evi.Name = v.Name;
                evi.Index = (int)ei.Variants.size();
                evi.Sp = v.Sp;
                CurModule = ed->Module;
                for (auto& f : v.Fields) {
                    FieldInfo fi;
                    fi.Name = f.Name;
                    fi.Ty = ResolveType(f.Ty, false, false);
                    fi.Sp = f.Sp;
                    auto sf = sharedFields.find(f.Name);
                    if (sf != sharedFields.end() && !IsErr(fi.Ty) &&
                        !sf->second.SameAs(fi.Ty)) {
                        Diag d = Rep.Err("E0603", f.Sp,
                            "el campo `" + f.Name + "` tiene tipos distintos en las variantes de `" +
                            DisplayName(key) + "`");
                        d.Helps.push_back("si dos variantes comparten el nombre de un campo, "
                                          "el tipo debe ser el mismo");
                        Rep.Emit(d);
                    } else if (sf == sharedFields.end()) {
                        sharedFields[f.Name] = fi.Ty;
                    }
                    evi.Fields.push_back(std::move(fi));
                }
                CurModule = "";
                VariantInfo vi;
                vi.EnumName = key;
                vi.VariantName = v.Name;
                vi.Index = (int)ei.Variants.size();
                vi.Sp = v.Sp;
                Variantes[v.Name] = vi;
                ei.Variants.push_back(std::move(evi));
            }
        }
        for (auto& fd : Prog->Fns) {
            UseModuleFile(fd->Module);
            if (fd->Foreign) ValidateForeign(*fd);
            std::string key = Mangle(fd->Module, fd->Name);
            if (fd->Name == "main" && fd->Module.empty()) {
                Diag d = Rep.Err("E0305", fd->Sp, "no se puede llamar `main` (en minúsculas)");
                d.Helps.push_back("el punto de entrada de un programa Sylt es `fn Main()`");
                Rep.Emit(d);
                continue;
            }
            if (fd->Name == "Main" && !fd->Module.empty()) {
                Diag d = Rep.Err("E0305", fd->Sp,
                    "`Main` solo puede estar en el archivo principal, no en un módulo");
                Rep.Emit(d);
                continue;
            }
            if (!fd->Foreign && StartsWith(fd->Name, "Sylt")) {
                Diag d = Rep.Err("E0305", fd->Sp,
                    "los nombres que empiezan por `Sylt` están reservados para el runtime");
                d.Helps.push_back("elige otro nombre sin el prefijo Sylt");
                Rep.Emit(d);
                continue;
            }
            if (!fd->Foreign && StartsWith(fd->Name, "sy_") &&
                !StartsWith(fd->Module, "std.")) {
                Diag d = Rep.Err("E0305", fd->Sp,
                    "los nombres que empiezan por `sy_` están reservados para la librería estándar");
                Rep.Emit(d);
                continue;
            }
            if (Fns.count(key) || Structs.count(key) || Enums.count(key) ||
                Generics.count(key) || IsBuiltin(fd->Name)) {
                Diag d = Rep.Err("E0305", fd->Sp, "`" + key + "` ya existe");
                if (IsBuiltin(fd->Name))
                    d.Msg += " (es una función integrada del lenguaje)";
                d.Helps.push_back("elige otro nombre para tu función");
                Rep.Emit(d);
                continue;
            }
            Fns[key] = fd.get();
            if (fd->Foreign && !Fns.count(fd->Name)) Fns[fd->Name] = fd.get();
        }
        for (auto& im : Prog->Imports) {
            if (Mods.count(im.Module)) continue;
            ModuleInfo mi;
            mi.Name = im.Module;
            mi.Sp = im.Sp;
            for (auto& fd : Prog->Fns) {
                if (fd->Module != im.Module) continue;
                mi.Fns[fd->Name] = FnKey(fd.get());
            }
            for (auto& td : Prog->Types)
                if (td->Module == im.Module)
                    mi.Types[td->Name] = Mangle(im.Module, td->Name);
            for (auto& ed : Prog->Enums)
                if (ed->Module == im.Module)
                    mi.Enums[ed->Name] = Mangle(im.Module, ed->Name);
            Mods[im.Module] = std::move(mi);
        }
        for (auto& td : Prog->Types) {
            UseModuleFile(td->Module);
            std::string key = Mangle(td->Module, td->Name);
            if (!Structs.count(key)) continue;
            StructInfo& si = Structs[key];
            std::unordered_set<std::string> seen;
            CurModule = td->Module;
            for (auto& f : td->Fields) {
                if (seen.count(f.Name)) {
                    Diag d = Rep.Err("E0305", f.Sp,
                        "el campo `" + f.Name + "` está repetido en `" + DisplayName(key) + "`");
                    Rep.Emit(d);
                    continue;
                }
                seen.insert(f.Name);
                FieldInfo fi;
                fi.Name = f.Name;
                fi.Ty = ResolveType(f.Ty, false, false);
                fi.HasDefault = f.HasDefault;
                fi.Sp = f.Sp;
                if (f.HasDefault && f.Default) {
                    Push();
                    CheckExpr(f.Default.get(), IsErr(fi.Ty) ? nullptr : &fi.Ty);
                    Pop();
                }
                si.Fields.push_back(std::move(fi));
            }
            for (auto& m : td->Methods) {
                if (ValidateMethodSig(*m)) {
                    continue;
                }
                if (si.Methods.count(m->Name)) {
                    Diag d = Rep.Err("E0305", m->Sp,
                        "el método `" + m->Name + "` está repetido en `" + DisplayName(key) + "`");
                    Rep.Emit(d);
                    continue;
                }
                si.Methods[m->Name] = m.get();
            }
            CurModule = "";
        }
        CheckStructCycles();
    }
    bool ForeignTypeOk(const TypeAnn& a) {
        switch (a.k) {
            case TypeAnn::K::Prim:
                return a.Name == "int" || a.Name == "float" ||
                       a.Name == "bool" || a.Name == "string";
            case TypeAnn::K::Array:
                if (a.Sub.empty()) return false;
                return ForeignTypeOk(a.Sub[0]);
            case TypeAnn::K::Map:
                if (a.Sub.size() < 2) return false;
                return ForeignTypeOk(a.Sub[0]) && ForeignTypeOk(a.Sub[1]);
            case TypeAnn::K::Tuple: {
                if (a.Sub.empty()) return false;
                for (auto& s : a.Sub)
                    if (!ForeignTypeOk(s)) return false;
                return true;
            }
            default: return false;
        }
    }
    void ValidateForeign(FnDecl& fd) {
        for (auto& p : fd.Params) {
            if (p.IsSelf || !ForeignTypeOk(p.Ty)) {
                Diag d = Rep.Err("E0801", p.Sp,
                    "los parámetros foreign solo admiten int, float, bool, string "
                    "y colecciones planas de estos ([T], {K: V}, tuplas)");
                d.Helps.push_back("los structs y enums viven en el mundo Sylt; "
                                  "para el límite C++ usa tipos planos");
                Rep.Emit(d);
            }
        }
        if (fd.HasRet && !ForeignTypeOk(fd.Ret)) {
            Diag d = Rep.Err("E0801", fd.Ret.Sp,
                "el retorno foreign solo admite int, float, bool, string "
                "y colecciones planas de estos ([T], {K: V}, tuplas)");
            d.Helps.push_back("si necesitas más, envuelve la llamada en una función Sylt normal");
            Rep.Emit(d);
        }
    }
    bool ValidateMethodSig(FnDecl& m) {
        bool bad = false;
        for (size_t i = 0; i < m.Params.size(); i++) {
            if (!m.Params[i].IsSelf) continue;
            if (i != 0) {
                Diag d = Rep.Err("E0201", m.Params[i].Sp,
                    "`self` debe ser el primer parámetro");
                Rep.Emit(d);
                bad = true;
            }
        }
        if (m.Params.empty() || !m.Params[0].IsSelf) {
            for (size_t i = 1; i < m.Params.size(); i++) {
                if (m.Params[i].IsSelf) {
                    Diag d = Rep.Err("E0201", m.Params[i].Sp,
                        "`self` solo puede ser el primer parámetro");
                    Rep.Emit(d);
                    bad = true;
                }
            }
        }
        return bad;
    }
    void CheckStructCycles() {
        std::unordered_map<std::string, std::vector<std::string>> deps;
        for (auto& kv : Structs) {
            for (auto& f : kv.second.Fields)
                if (f.Ty.IsStruct()) deps[kv.first].push_back(f.Ty.Name);
        }
        std::unordered_set<std::string> visiting, done;
        std::vector<std::string> path;
        std::function<void(const std::string&)> dfs = [&](const std::string& n) {
            if (done.count(n) || (Rep.ErrorCount() > 0 && Failed)) return;
            if (visiting.count(n)) {
                std::string cyc;
                bool started = false;
                for (auto& p : path) {
                    if (p == n) started = true;
                    if (started) cyc += (cyc.empty() ? "" : " -> ") + p;
                }
                cyc += " -> " + n;
                auto f = Structs.find(n);
                Span sp = f != Structs.end() ? f->second.Sp : Span();
                Diag d = Rep.Err("E0407", sp,
                    "`" + n + "` se contiene a sí mismo a través de sus campos (" + cyc + ")");
                d.Helps.push_back("guarda las relaciones con arrays: `[Pieza]` dentro de otro struct");
                Rep.Emit(d);
                Failed = true;
                return;
            }
            visiting.insert(n);
            path.push_back(n);
            auto f = deps.find(n);
            if (f != deps.end())
                for (auto& d2 : f->second) dfs(d2);
            path.pop_back();
            visiting.erase(n);
            done.insert(n);
        };
        for (auto& kv : Structs) dfs(kv.first);
    }
    bool RequireMain = true;
    void CheckBodies() {
        for (auto& td : Prog->Types) {
            if (!td->TypeParams.empty()) continue;
            std::string key = Mangle(td->Module, td->Name);
            auto f = Structs.find(key);
            if (f == Structs.end()) continue;
            for (auto& m : td->Methods) {
                if (!f->second.Methods.count(m->Name)) continue;
                CheckFn(m.get(), &key);
            }
        }
        for (auto& fd : Prog->Fns) {
            if (fd->IsMethod) continue;
            auto f = Fns.find(FnKey(fd.get()));
            if (f == Fns.end() || f->second != fd.get()) continue;
            CheckFn(fd.get(), nullptr);
        }
        bool hasMain = Fns.count("Main") > 0;
        for (auto& td : Prog->Types) {
            auto f = Structs.find(Mangle(td->Module, td->Name));
            if (f == Structs.end()) continue;
            if (f->second.Methods.count("Main")) {
                Diag d = Rep.Err("E0305", td->Sp,
                    "el punto de entrada `Main` debe ser una función global, no un método");
                d.Helps.push_back("declara `fn Main()` fuera del struct");
                Rep.Emit(d);
            }
        }
        if (!hasMain && RequireMain) {
            Diag d = Rep.ErrNoSpan("E0302", "no encuentro `fn Main()`");
            d.Helps.push_back("Main es el punto de entrada: Sylt genera el ejecutable a partir de él");
            d.Helps.push_back("si este archivo es un módulo, `syltc check` lo acepta sin Main");
            d.Helps.push_back("si este archivo contiene pruebas, `syltc test archivo.sy` las ejecuta sin Main");
            Rep.Emit(d);
        }
    }
    void CheckInstantiations() {
        CheckingInstantiation = true;
        int guard = 0;
        while (!PendingCheck.empty() && guard++ < 500) {
            auto item = PendingCheck.back();
            PendingCheck.pop_back();
            CheckFn(item.second, &item.first);
        }
        PendingCheck.clear();
    }
    void CheckFn(FnDecl* fd, const std::string* owner) {
        if (fd->Foreign) return;
        UseModuleFile(fd->Module);
        CurFn = fd;
        CurModule = fd->Module;
        Push();
        bool sawSelf = false;
        for (auto& p : fd->Params) {
            if (p.IsSelf) {
                sawSelf = true;
                VarInfo v;
                v.Ty = Type::StructOf(*owner);
                v.Mutable = p.Mut;
                v.Moveable = false;
                v.Used = true;
                v.DeclSp = p.Sp;
                v.KindV = VarInfo::Kind::Self;
                Scopes.back()["self"] = std::move(v);
                continue;
            }
            VarInfo v;
            Type pty = ResolveType(p.Ty, true, false);
            if (pty.IsBorrow()) {
                v.Ty = pty.Pointee();
                v.Mutable = pty.Mut;
                v.Moveable = false;
                v.IsBorrowParam = true;
                v.KindV = VarInfo::Kind::Param;
            } else {
                v.Ty = pty;
                v.Mutable = p.Mut;
                v.Moveable = v.Ty.IsStruct();
                v.KindV = VarInfo::Kind::Param;
            }
            v.DeclSp = p.Sp;
            Scopes.back()[p.Name] = std::move(v);
        }
        if (!owner && sawSelf) {
            Diag d = Rep.Err("E0201", fd->Sp,
                "`self` solo existe dentro de los métodos de un struct");
            d.Helps.push_back("para usar `" + fd->Name +
                              "`, decláralo dentro de un `type` o quita self");
            Rep.Emit(d);
        }
        CurRet = fd->HasRet ? ResolveType(fd->Ret, false, true) : Type::Void();
        CurRetIsVoid = CurRet.IsVoid();
        if (fd->HasRet && CurRet.IsBorrow()) {
            Rep.Emit(Rep.Err("E0408", fd->Ret.Sp,
                "una función no puede devolver un préstamo (por ahora)"));
        }
        InLoop = 0;
        WalkBlock(fd->Body);
        WarnUnusedLocals();
        if (!CurRetIsVoid && !IsErr(CurRet)) {
            bool anyReturn = false;
            std::function<void(const Block&)> scan = [&](const Block& b) {
                for (auto& s : b.Stmts) {
                    if (s->K == Stmt::Kind::Return) anyReturn = true;
                    if (s->K == Stmt::Kind::If) {
                        auto* i = static_cast<IfStmt*>(s.get());
                        scan(i->Then);
                        for (auto& br : i->Elifs) scan(br.Body);
                        if (i->HasElse) scan(i->Else);
                    }
                    if (s->K == Stmt::Kind::While) scan(static_cast<WhileStmt*>(s.get())->Body);
                    if (s->K == Stmt::Kind::For) scan(static_cast<ForStmt*>(s.get())->Body);
                    if (s->K == Stmt::Kind::Match) {
                        auto* m = static_cast<MatchStmt*>(s.get());
                        for (auto& arm : m->Arms) scan(arm.Body);
                    }
                }
            };
            scan(fd->Body);
            if (!anyReturn) {
                Diag d = Rep.Err("E0406", fd->Sp,
                    "`" + fd->Name + "` declara devolver `" + TypeName(CurRet) +
                    "`, pero ninguna rama hace `return`");
                d.Helps.push_back("añade `return <valor>` al final de la función");
                Rep.Emit(d);
            }
        }
        Pop();
        CurModule = "";
        CurFn = nullptr;
    }
    void WarnUnusedLocals() {
        if (CheckingInstantiation) return;
        for (auto& sc : Scopes) {
            for (auto& kv : sc) {
                VarInfo& v = kv.second;
                if (v.Used) continue;
                if (!kv.first.empty() && kv.first[0] == '_') continue;
                if (v.KindV == VarInfo::Kind::Let || v.KindV == VarInfo::Kind::Var ||
                    v.KindV == VarInfo::Kind::LoopVar) {
                    Diag d = Rep.Warn("W0001", v.DeclSp,
                        "`" + kv.first + "` se declara pero no se usa");
                    d.Helps.push_back("si es intencional, nómbralo `_" + kv.first + "`");
                    Rep.Emit(d);
                }
            }
        }
    }
    void WalkBlock(const Block& b) {
        for (auto& s : b.Stmts) WalkStmt(s.get());
    }
    void WalkStmt(Stmt* st) {
        switch (st->K) {
            case Stmt::Kind::Let: {
                auto* s = static_cast<LetStmt*>(st);
                Type wantTy;
                if (s->HasTy) wantTy = ResolveType(s->Ty, false, false);
                Type initTy = CheckExpr(s->Init.get(), s->HasTy ? &wantTy : nullptr);
                if (s->HasTy) {
                    if (!AssignableTo(initTy, wantTy)) TypeMismatch(s->Init->Sp, wantTy, initTy);
                    s->VarType = IsErr(wantTy) ? initTy : wantTy;
                } else {
                    s->VarType = initTy;
                }
                if (s->Destructure) {
                    if (!IsErr(initTy) && !initTy.IsTuple()) {
                        Diag d = Rep.Err("E0401", s->Init->Sp,
                            "solo se puede desestructurar una tupla, pero aquí hay `" +
                            TypeName(initTy) + "`");
                        d.Helps.push_back("las funciones devuelven tuplas así: "
                                          "`fn MinMax(A: int, B: int) -> (int, int)`");
                        Rep.Emit(d);
                    } else if (initTy.IsTuple() && initTy.Sub.size() != s->Names.size()) {
                        Diag d = Rep.Err("E0401", s->Init->Sp,
                            "la tupla tiene " + std::to_string(initTy.Sub.size()) +
                            " elementos, pero desestructuras " + std::to_string(s->Names.size()));
                        d.Helps.push_back("usa `_` para los elementos que no quieras: `let (A, _) = T`");
                        Rep.Emit(d);
                    }
                    for (size_t i = 0; i < s->Names.size(); i++) {
                        if (s->Names[i] == "_") continue;
                        VarInfo v;
                        v.Mutable = s->Mutable;
                        if (initTy.IsTuple() && i < initTy.Sub.size()) v.Ty = initTy.Sub[i];
                        else v.Ty = Type::Err();
                        v.Moveable = false;
                        v.DeclSp = s->Sp;
                        v.KindV = s->Mutable ? VarInfo::Kind::Var : VarInfo::Kind::Let;
                        Bind(s->Names[i], std::move(v), s->Sp);
                    }
                    return;
                }
                const std::string& name = s->Names[0];
                bool willMove = s->VarType.IsStruct();
                if (name != "_") {
                    VarInfo v;
                    v.Mutable = s->Mutable;
                    v.Ty = s->VarType;
                    v.Moveable = willMove;
                    v.DeclSp = s->Sp;
                    v.KindV = s->Mutable ? VarInfo::Kind::Var : VarInfo::Kind::Let;
                    Bind(name, std::move(v), s->Sp);
                }
                if (willMove) MoveVar(s->Init.get());
                return;
            }
            case Stmt::Kind::Assign: {
                auto* s = static_cast<AssignStmt*>(st);
                Type targetTy = CheckAssignTarget(s->Target.get());
                Type valTy = CheckExpr(s->Value.get(), IsErr(targetTy) ? nullptr : &targetTy);
                if (!IsErr(targetTy) && !IsErr(valTy)) {
                    if (s->Op != "=") {
                        bool ok = (targetTy.IsNumeric() && valTy.IsNumeric()) ||
                                  (s->Op == "+=" && targetTy.IsStr() && valTy.IsStr());
                        if (!ok) {
                            Diag d = Rep.Err("E0401", s->Sp,
                                "no puedo aplicar `" + s->Op + "` entre `" +
                                TypeName(targetTy) + "` y `" + TypeName(valTy) + "`");
                            d.Helps.push_back(targetTy.IsStr()
                                ? "para juntar textos usa la suma de strings o interpolación"
                                : "`+= -= *= /=` funcionan con números");
                            Rep.Emit(d);
                        }
                    } else if (!AssignableTo(valTy, targetTy)) {
                        TypeMismatch(s->Value->Sp, targetTy, valTy);
                    }
                }
                if (s->Target->K == Expr::Kind::Ident && valTy.IsStruct()) {
                    auto* tgt = static_cast<IdentExpr*>(s->Target.get());
                    VarInfo* tv = FindVar(tgt->Name);
                    if (tv) tv->Moved = false;
                }
                if (valTy.IsStruct() && s->Value->K == Expr::Kind::Ident)
                    s->ValMoved = MoveVar(s->Value.get());
                return;
            }
            case Stmt::Kind::Expr: {
                auto* s = static_cast<ExprStmt*>(st);
                CheckExpr(s->Sub.get());
                return;
            }
            case Stmt::Kind::Return: {
                auto* s = static_cast<ReturnStmt*>(st);
                if (!CurFn) {
                    Rep.Emit(Rep.Err("E0201", s->Sp,
                        "`return` solo tiene sentido dentro de una función"));
                    return;
                }
                if (s->Value) {
                    Type t = CheckExpr(s->Value.get(), IsErr(CurRet) ? nullptr : &CurRet);
                    if (!CurRetIsVoid && !IsErr(CurRet) && !IsErr(t) && !AssignableTo(t, CurRet))
                        TypeMismatch(s->Value->Sp, CurRet, t);
                    if (CurRetIsVoid && !IsErr(t)) {
                        Diag d = Rep.Err("E0401", s->Value->Sp,
                            "`" + CurFn->Name + "` es de tipo void: no devuelve nada");
                        d.Helps.push_back("escribe `return` a secas, o cambia el tipo de retorno "
                                          "a `-> " + TypeName(t) + "`");
                        Rep.Emit(d);
                    }
                    if (t.IsStruct() && s->Value->K == Expr::Kind::Ident)
                        MoveVar(s->Value.get());
                } else if (!CurRetIsVoid && !IsErr(CurRet)) {
                    Diag d = Rep.Err("E0401", s->Sp,
                        "falta el valor de retorno: `" + CurFn->Name + "` debe devolver `" +
                        TypeName(CurRet) + "`");
                    d.Helps.push_back("escribe `return <valor>`");
                    Rep.Emit(d);
                }
                return;
            }
            case Stmt::Kind::If: {
                auto* s = static_cast<IfStmt*>(st);
                CheckCond(s->Cond.get(), "if");
                MoveSnap acum = Scopes;
                WalkBranch(s->Then, acum);
                for (auto& br : s->Elifs) {
                    CheckCond(br.Cond.get(), "elif");
                    WalkBranch(br.Body, acum);
                }
                if (s->HasElse) WalkBranch(s->Else, acum);
                Scopes = std::move(acum);
                return;
            }
            case Stmt::Kind::While: {
                auto* s = static_cast<WhileStmt*>(st);
                CheckCond(s->Cond.get(), "while");
                MoveSnap entry = Scopes;
                size_t depth = Scopes.size();
                InLoop++;
                Rep.Quiet++;
                for (int pass = 0; pass < 12; pass++) {
                    Scopes = entry;
                    Push();
                    WalkBlock(s->Body);
                    Pop();
                    MoveSnap next = entry;
                    MergeMovesInto(next, depth);
                    if (SameMoves(next, entry, depth)) break;
                    entry = std::move(next);
                }
                Rep.Quiet--;
                Scopes = entry;
                Push();
                WalkBlock(s->Body);
                Pop();
                InLoop--;
                return;
            }
            case Stmt::Kind::For: {
                auto* s = static_cast<ForStmt*>(st);
                Type iterTy = CheckExpr(s->Low.get());
                VarInfo v;
                if (s->IsRange) {
                    Type hiTy = CheckExpr(s->High.get());
                    if (!IsErr(iterTy) && !iterTy.IsInt()) {
                        Diag d = Rep.Err("E0401", s->Low->Sp,
                            "el rango `A..B` necesita enteros, pero A es `" +
                            TypeName(iterTy) + "`");
                        d.Helps.push_back("los rangos son siempre de enteros: `for I in 0..N`");
                        Rep.Emit(d);
                    }
                    if (!IsErr(hiTy) && !hiTy.IsInt()) {
                        Diag d = Rep.Err("E0401", s->High->Sp,
                            "el rango `A..B` necesita enteros, pero B es `" +
                            TypeName(hiTy) + "`");
                        Rep.Emit(d);
                    }
                    v.Ty = Type::Int();
                } else if (!IsErr(iterTy)) {
                    if (iterTy.IsArray()) {
                        v.Ty = iterTy.Sub[0];
                    } else if (iterTy.IsMap()) {
                        v.Ty = iterTy.Sub[0];
                    } else {
                        Diag d = Rep.Err("E0405", s->Low->Sp,
                            "no puedes iterar sobre `" + TypeName(iterTy) + "`");
                        d.Helps.push_back("puedes iterar un rango (0..N), un array [T] "
                                          "o un mapa (itera sus claves)");
                        Rep.Emit(d);
                        v.Ty = Type::Err();
                    }
                } else {
                    v.Ty = Type::Err();
                }
                v.Mutable = false;
                v.Moveable = false;
                v.DeclSp = s->Sp;
                v.KindV = VarInfo::Kind::LoopVar;
                Push();
                Bind(s->Var, std::move(v), s->Sp);
                MoveSnap entry = Scopes;
                size_t depth = Scopes.size();
                InLoop++;
                Rep.Quiet++;
                for (int pass = 0; pass < 12; pass++) {
                    Scopes = entry;
                    WalkBlock(s->Body);
                    MoveSnap next = entry;
                    MergeMovesInto(next, depth);
                    if (SameMoves(next, entry, depth)) break;
                    entry = std::move(next);
                }
                Rep.Quiet--;
                Scopes = entry;
                WalkBlock(s->Body);
                InLoop--;
                Pop();
                return;
            }
            case Stmt::Kind::Break:
            case Stmt::Kind::Continue: {
                if (InLoop == 0) {
                    const char* kw = (st->K == Stmt::Kind::Break) ? "break" : "continue";
                    Diag d = Rep.Err("E0201", st->Sp,
                        std::string("`") + kw +
                        "` solo tiene sentido dentro de un bucle (for/while)");
                    Rep.Emit(d);
                }
                return;
            }
            case Stmt::Kind::Pass:
                return;
            case Stmt::Kind::Match:
                CheckMatch(static_cast<MatchStmt*>(st));
                return;
        }
    }
    void CheckCond(Expr* e, const char* ctx) {
        Type t = CheckExpr(e);
        if (IsErr(t) || t.IsBool()) return;
        Diag d = Rep.Err("E0404", e->Sp,
            std::string("la condición del `") + ctx + "` debe ser `bool`, pero es `" +
            TypeName(t) + "`");
        if (t.IsInt()) d.Helps.push_back("escribe la comparación completa: `X != 0`");
        else if (t.IsFloat()) d.Helps.push_back("escribe la comparación completa: `X > 0.0`");
        Rep.Emit(d);
    }
    Type CheckAssignTarget(Expr* e) {
        if (e->K == Expr::Kind::Ident) {
            auto* id = static_cast<IdentExpr*>(e);
            VarInfo* v = FindVar(id->Name);
            if (!v) {
                Rep.Emit(UnknownName("E0301", id->Sp, "la variable", id->Name));
                return Type::Err();
            }
            CheckMutableTarget(e);
            id->Ty = v->Ty;
            e->Ty = v->Ty;
            return v->Ty;
        }
        if (e->K == Expr::Kind::Index || e->K == Expr::Kind::Member) {
            Type t = CheckExpr(e);
            CheckMutableTarget(e);
            return t;
        }
        Rep.Emit(Rep.Err("E0202", e->Sp, "aquí no se puede asignar"));
        return Type::Err();
    }
    Type CheckExpr(Expr* e, const Type* want = nullptr) {
        Type t = CheckExprInner(e, want);
        e->Ty = t;
        return t;
    }
    Type CheckExprInner(Expr* e, const Type* want) {
        switch (e->K) {
            case Expr::Kind::IntLit: return Type::Int();
            case Expr::Kind::FloatLit: return Type::Float();
            case Expr::Kind::BoolLit: return Type::Bool();
            case Expr::Kind::StrLit: {
                auto* s = static_cast<StrLitExpr*>(e);
                for (auto& p : s->Parts)
                    if (!p.IsText) CheckExpr(p.Sub.get());
                return Type::Str();
            }
            case Expr::Kind::Ident: {
                auto* id = static_cast<IdentExpr*>(e);
                if (Mods.count(id->Name)) {
                    Diag d = Rep.Err("E0304", id->Sp,
                        "`" + id->Name + "` es un módulo, no un valor");
                    d.Helps.push_back("llama a sus funciones: `" + id->Name +
                                      ".Funcion(...)` o usa `" + id->Name +
                                      ".Tipo` en las anotaciones de tipo");
                    Rep.Emit(d);
                    return Type::Err();
                }
                if (IsBuiltin(id->Name) && !FindVar(id->Name)) {
                    Diag d = Rep.Err("E0301", id->Sp,
                        "`" + id->Name + "` es una función integrada: hay que llamarla");
                    d.Helps.push_back("escríbela con paréntesis: `" + id->Name + "(...)`");
                    Rep.Emit(d);
                    return Type::Err();
                }
                VarInfo* v = FindVar(id->Name);
                if (!v) {
                    std::string gk = FindGenericKey(id->Name);
                    if (!gk.empty()) {
                        Diag d = Rep.Err("E0310", id->Sp,
                            "`" + id->Name + "` es un struct genérico, no un valor");
                        d.Helps.push_back("constrúyelo con sus tipos: " + id->Name +
                                          "[int](...)");
                        Rep.Emit(d);
                        return Type::Err();
                    }
                    Rep.Emit(UnknownName("E0301", id->Sp, "la variable", id->Name));
                    return Type::Err();
                }
                v->Used = true;
                if (v->Moved) {
                    Diag d = Rep.Err("E0501", id->Sp,
                        "no puedes usar `" + id->Name + "` porque su valor ya se movió");
                    d.Notes.push_back({Diag::NoteKind::Note,
                        "el valor se movió en esta otra línea", v->MoveSp});
                    d.Helps.push_back("los structs no se copian: se mueven. Si quieres conservar "
                                      "el original, escribe `Clone(" + id->Name +
                                      ")` en el punto de movimiento");
                    Rep.Emit(d);
                    return Type::Err();
                }
                return v->Ty;
            }
            case Expr::Kind::Unary: {
                auto* u = static_cast<UnaryExpr*>(e);
                Type t = CheckExpr(u->Sub.get());
                if (u->Op == "-") {
                    if (!IsErr(t) && !t.IsNumeric()) {
                        Diag d = Rep.Err("E0401", e->Sp,
                            "el signo `-` va delante de números, pero esto es `" +
                            TypeName(t) + "`");
                        Rep.Emit(d);
                        return Type::Err();
                    }
                    return t;
                }
                if (!IsErr(t) && !t.IsBool()) {
                    Diag d = Rep.Err("E0401", e->Sp,
                        "`not` necesita un `bool`, pero esto es `" + TypeName(t) + "`");
                    d.Helps.push_back("si quieres negar una comparación: `not (X == Y)`");
                    Rep.Emit(d);
                    return Type::Err();
                }
                return Type::Bool();
            }
            case Expr::Kind::Binary:
                return CheckBinary(static_cast<BinaryExpr*>(e));
            case Expr::Kind::Call:
                return CheckCall(static_cast<CallExpr*>(e));
            case Expr::Kind::Index:
                return CheckIndex(static_cast<IndexExpr*>(e));
            case Expr::Kind::Member:
                return CheckMember(static_cast<MemberExpr*>(e));
            case Expr::Kind::ArrayLit: {
                auto* a = static_cast<ArrayLitExpr*>(e);
                if (a->Elems.empty()) {
                    if (want && want->IsArray()) return *want;
                    Diag d = Rep.Err("E0402", a->Sp,
                        "no puedo inferir el tipo de un array vacío");
                    d.Helps.push_back("anota el tipo: `let X: [int] = []`");
                    Rep.Emit(d);
                    return Type::Err();
                }
                Type elem = CheckExpr(a->Elems[0].get());
                a->ElemsMoved.assign(a->Elems.size(), false);
                if (elem.IsStruct())
                    a->ElemsMoved[0] = MoveVar(a->Elems[0].get());
                for (size_t i = 1; i < a->Elems.size(); i++) {
                    Type t = CheckExpr(a->Elems[i].get(), &elem);
                    if (!IsErr(elem) && !IsErr(t) && !AssignableTo(t, elem) &&
                        !AssignableTo(elem, t)) {
                        Diag d = Rep.Err("E0401", a->Elems[i]->Sp,
                            "este elemento es `" + TypeName(t) + "`, pero el primero fue `" +
                            TypeName(elem) + "`");
                        d.Helps.push_back("todos los elementos de un array son del mismo tipo");
                        Rep.Emit(d);
                    }
                    if (elem.IsInt() && t.IsFloat()) elem = t;
                    if (t.IsStruct()) a->ElemsMoved[i] = MoveVar(a->Elems[i].get());
                }
                if (IsErr(elem)) return Type::Err();
                return Type::ArrayOf(elem);
            }
            case Expr::Kind::MapLit: {
                auto* m = static_cast<MapLitExpr*>(e);
                if (m->Pairs.empty()) {
                    if (want && want->IsMap()) return *want;
                    Diag d = Rep.Err("E0402", m->Sp,
                        "no puedo inferir el tipo de un mapa vacío");
                    d.Helps.push_back("anota el tipo: `let M: {string: int} = {}`");
                    Rep.Emit(d);
                    return Type::Err();
                }
                Type kt = CheckExpr(m->Pairs[0].Key.get());
                Type vt = CheckExpr(m->Pairs[0].Val.get());
                m->Pairs[0].ValMoved = vt.IsStruct() && MoveVar(m->Pairs[0].Val.get());
                if (!IsErr(kt) && !kt.IsStr() && !kt.IsInt()) {
                    Diag d = Rep.Err("E0401", m->Pairs[0].Key->Sp,
                        "las claves de un mapa deben ser `string` o `int`, pero esta es `" +
                        TypeName(kt) + "`");
                    Rep.Emit(d);
                    kt = Type::Err();
                }
                for (size_t i = 1; i < m->Pairs.size(); i++) {
                    Type k = CheckExpr(m->Pairs[i].Key.get(), &kt);
                    Type v = CheckExpr(m->Pairs[i].Val.get(), &vt);
                    m->Pairs[i].ValMoved = v.IsStruct() && MoveVar(m->Pairs[i].Val.get());
                    if (!IsErr(kt) && !IsErr(k) && !AssignableTo(k, kt)) {
                        Diag d = Rep.Err("E0401", m->Pairs[i].Key->Sp,
                            "esta clave es `" + TypeName(k) + "`, pero la primera fue `" +
                            TypeName(kt) + "`");
                        Rep.Emit(d);
                    }
                    if (!IsErr(vt) && !IsErr(v) && !AssignableTo(v, vt) &&
                        !AssignableTo(vt, v)) {
                        Diag d = Rep.Err("E0401", m->Pairs[i].Val->Sp,
                            "este valor es `" + TypeName(v) + "`, pero el primero fue `" +
                            TypeName(vt) + "`");
                        d.Helps.push_back("todos los valores de un mapa son del mismo tipo");
                        Rep.Emit(d);
                    }
                }
                if (IsErr(kt) || IsErr(vt)) return Type::Err();
                return Type::MapOf(kt, vt);
            }
            case Expr::Kind::TupleLit: {
                auto* t = static_cast<TupleLitExpr*>(e);
                std::vector<Type> ms;
                for (auto& el : t->Elems) ms.push_back(CheckExpr(el.get()));
                return Type::TupleOf(std::move(ms));
            }
        }
        return Type::Err();
    }
    Type CheckBinary(BinaryExpr* b) {
        Type lt = CheckExpr(b->L.get());
        Type rt = CheckExpr(b->R.get());
        const std::string& op = b->Op;
        if (op == "and" || op == "or") {
            if (!IsErr(lt) && !lt.IsBool()) {
                Diag d = Rep.Err("E0401", b->L->Sp,
                    "`" + op + "` necesita bools, pero la izquierda es `" + TypeName(lt) + "`");
                d.Helps.push_back("si quieres comprobar que no es cero: `X != 0`");
                Rep.Emit(d);
            }
            if (!IsErr(rt) && !rt.IsBool()) {
                Diag d = Rep.Err("E0401", b->R->Sp,
                    "`" + op + "` necesita bools, pero la derecha es `" + TypeName(rt) + "`");
                d.Helps.push_back("si quieres comprobar que no es cero: `Y != 0`");
                Rep.Emit(d);
            }
            return Type::Bool();
        }
        if (op == "+" && (lt.IsStr() || rt.IsStr())) {
            if (!IsErr(lt) && !IsErr(rt) && !(lt.IsStr() && rt.IsStr())) {
                Diag d = Rep.Err("E0401", b->Sp,
                    "no puedo sumar `" + TypeName(lt) + "` y `" + TypeName(rt) + "`");
                d.Helps.push_back("convierte con `Str(...)`, o mejor, usa interpolación: \"{A} {B}\"");
                Rep.Emit(d);
            }
            return Type::Str();
        }
        if (op == "==" || op == "!=") {
            bool ok = (lt.IsNumeric() && rt.IsNumeric()) || (lt.IsStr() && rt.IsStr()) ||
                      (lt.IsBool() && rt.IsBool());
            if (!IsErr(lt) && !IsErr(rt) && !ok) {
                Diag d = Rep.Err("E0401", b->Sp,
                    "no puedo comparar `" + TypeName(lt) + "` con `" + TypeName(rt) + "`");
                d.Helps.push_back("compara valores del mismo tipo; para structs, compara campo a campo");
                Rep.Emit(d);
            }
            return Type::Bool();
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            bool ok = (lt.IsNumeric() && rt.IsNumeric()) || (lt.IsStr() && rt.IsStr());
            if (!IsErr(lt) && !IsErr(rt) && !ok) {
                Diag d = Rep.Err("E0401", b->Sp,
                    "no puedo ordenar `" + TypeName(lt) + "` con `" + TypeName(rt) + "`");
                d.Helps.push_back("< <= > >= funcionan con números y con strings (orden alfabético)");
                Rep.Emit(d);
            }
            return Type::Bool();
        }
        if (op == "%") {
            if (!IsErr(lt) && !IsErr(rt) && !(lt.IsInt() && rt.IsInt())) {
                Diag d = Rep.Err("E0401", b->Sp, "el resto `%` solo funciona con enteros");
                d.Helps.push_back("para decimales, usa la parte entera con Floor");
                Rep.Emit(d);
            }
            return Type::Int();
        }
        if (!IsErr(lt) && !IsErr(rt) && !(lt.IsNumeric() && rt.IsNumeric())) {
            Diag d = Rep.Err("E0401", b->Sp,
                "no puedo operar `" + TypeName(lt) + " " + op + " " + TypeName(rt) + "`");
            if (lt.IsStr() || rt.IsStr())
                d.Helps.push_back("los textos solo se juntan con `+` entre strings; "
                                  "para mezclar usa interpolación");
            if (lt.IsArray() || rt.IsArray())
                d.Helps.push_back("para añadir a un array usa `Lista.Push(X)`");
            Rep.Emit(d);
            return Type::Err();
        }
        if (lt.IsFloat() || rt.IsFloat()) return Type::Float();
        return Type::Int();
    }
    Type CheckIndex(IndexExpr* ix) {
        Type base = CheckExpr(ix->Base.get());
        Type idx = CheckExpr(ix->Idx.get());
        if (IsErr(base)) return Type::Err();
        if (base.IsArray()) {
            if (!IsErr(idx) && !idx.IsInt()) {
                Diag d = Rep.Err("E0401", ix->Idx->Sp,
                    "el índice de un array debe ser `int`, pero es `" + TypeName(idx) + "`");
                Rep.Emit(d);
            }
            return base.Sub[0];
        }
        if (base.IsMap()) {
            if (!IsErr(idx) && !IsErr(base.Sub[0]) && !AssignableTo(idx, base.Sub[0])) {
                Diag d = Rep.Err("E0401", ix->Idx->Sp,
                    "este mapa usa claves `" + TypeName(base.Sub[0]) + "`, pero buscas con `" +
                    TypeName(idx) + "`");
                Rep.Emit(d);
            }
            return base.Sub[1];
        }
        if (base.IsTuple()) {
            if (ix->Idx->K != Expr::Kind::IntLit) {
                Diag d = Rep.Err("E0401", ix->Idx->Sp,
                    "para acceder a una tupla se usa una posición fija: T[0], T[1]...");
                d.Helps.push_back("la forma cómoda es desestructurar: `let (A, B) = T`");
                Rep.Emit(d);
                return Type::Err();
            }
            int64_t pos = static_cast<IntLitExpr*>(ix->Idx.get())->Val;
            if (pos < 0 || pos >= (int64_t)base.Sub.size()) {
                Diag d = Rep.Err("E0402", ix->Idx->Sp,
                    "la tupla tiene " + std::to_string(base.Sub.size()) +
                    " elementos, pero pides la posición " + std::to_string(pos));
                Rep.Emit(d);
                return Type::Err();
            }
            ix->TupleIndex = true;
            ix->TuplePos = (int)pos;
            return base.Sub[pos];
        }
        if (base.IsStr()) {
            Diag d = Rep.Err("E0402", ix->Base->Sp, "los strings no se indexan (por ahora)");
            d.Helps.push_back("usa `Texto.Contiene(\"...\")` para buscar dentro de un string");
            Rep.Emit(d);
            return Type::Err();
        }
        Diag d = Rep.Err("E0402", ix->Base->Sp, "no se puede indexar `" + TypeName(base) + "`");
        d.Helps.push_back("se indexan arrays [T], mapas {K: V} y tuplas");
        Rep.Emit(d);
        return Type::Err();
    }
    Type CheckMember(MemberExpr* m) {
        std::string modPath = ModulePathOf(m->Base.get());
        if (!modPath.empty()) {
            Diag d = Rep.Err("E0304", m->NameSp,
                "`" + modPath + "." + m->Name + "` no es un valor");
            d.Helps.push_back("llama a lo que exporta el módulo: `" + modPath + "." +
                              m->Name + "(...)`");
            Rep.Emit(d);
            return Type::Err();
        }
        if (m->Base->K == Expr::Kind::Ident) {
            auto* id = static_cast<IdentExpr*>(m->Base.get());
            if (Mods.count(id->Name)) {
                Diag d = Rep.Err("E0304", m->NameSp,
                    "`" + id->Name + "." + m->Name + "` no es un valor");
                d.Helps.push_back("llama a lo que exporta el módulo: `" + id->Name + "." +
                                  m->Name + "(...)`");
                Rep.Emit(d);
                return Type::Err();
            }
            if (EnumInfo* ei = FindEnum(id->Name)) {
                if (FindVariantIn(*ei, m->Name)) {
                    m->IsEnumUnit = true;
                    m->EnumKey = ei->Name;
                    return Type::EnumOf(ei->Name);
                }
                Diag d = Rep.Err("E0602", m->NameSp,
                    "el enum `" + DisplayName(ei->Name) + "` no tiene ninguna variante `" +
                    m->Name + "`");
                std::string list;
                for (auto& v : ei->Variants) list += (list.empty() ? "" : ", ") + v.Name;
                if (!list.empty()) d.Helps.push_back("sus variantes son: " + list);
                Rep.Emit(d);
                return Type::Err();
            }
        }
        Type base = CheckExpr(m->Base.get());
        if (IsErr(base)) return Type::Err();
        Type pointee = base.Pointee();
        if (pointee.IsStruct()) {
            auto f = Structs.find(pointee.Name);
            if (f == Structs.end()) return Type::Err();
            auto ff = std::find_if(f->second.Fields.begin(), f->second.Fields.end(),
                                   [&](const FieldInfo& fi) { return fi.Name == m->Name; });
            if (ff != f->second.Fields.end()) return ff->Ty;
            if (f->second.Methods.count(m->Name)) {
                Diag d = Rep.Err("E0304", m->NameSp,
                    "`" + m->Name + "` es un método: hay que llamarlo con paréntesis");
                d.Helps.push_back("escribe `" + m->Name + "(...)`");
                Rep.Emit(d);
                return Type::Err();
            }
            Diag d = Rep.Err("E0304", m->NameSp,
                "`" + f->second.Name + "` no tiene ningún campo `" + m->Name + "`");
            std::string list;
            for (auto& fi : f->second.Fields) list += (list.empty() ? "" : ", ") + fi.Name;
            if (!list.empty()) d.Helps.push_back("sus campos son: " + list);
            std::string best;
            int bd = 1 << 30;
            for (auto& fi : f->second.Fields) {
                if (!CloseEnough(m->Name, fi.Name)) continue;
                int dd = Levenshtein(m->Name, fi.Name);
                if (dd < bd) { bd = dd; best = fi.Name; }
            }
            if (!best.empty()) d.Helps.push_back("¿quisiste decir `" + best + "`?");
            Rep.Emit(d);
            return Type::Err();
        }
        Diag d = Rep.Err("E0304", m->Base->Sp,
            "`" + TypeName(base) + "` no tiene campos ni métodos `" + m->Name + "`");
        Rep.Emit(d);
        return Type::Err();
    }
    Type CheckCall(CallExpr* c) {
        if (c->Callee->K == Expr::Kind::Ident) {
            auto* id = static_cast<IdentExpr*>(c->Callee.get());
            if (!c->TypeArgs.empty()) return CheckGenericCall(c, id->Name, id->Name);
            if (IsBuiltin(id->Name)) return CheckBuiltinCall(c, id->Name);
            if (FnDecl* fd = FindFn(id->Name)) return CheckUserCall(c, fd);
            if (StructInfo* si = FindStruct(id->Name)) return CheckStructInit(c, *si);
            if (VariantInfo* vi = FindVariant(id->Name)) return CheckEnumCtor(c, *vi);
            {
                std::string gk = FindGenericKey(id->Name);
                if (!gk.empty()) {
                    Diag d = Rep.Err("E0310", c->Callee->Sp,
                        "el struct `" + id->Name + "` es genérico: indica sus tipos entre corchetes");
                    d.Helps.push_back("ejemplo: " + id->Name + "[int](...)");
                    Rep.Emit(d);
                    for (auto& a : c->Args) CheckExpr(a.Value.get());
                    return Type::Err();
                }
            }
            Rep.Emit(UnknownName("E0302", id->Sp, "la función", id->Name));
            return Type::Err();
        }
        if (c->Callee->K == Expr::Kind::Member) {
            auto* m = static_cast<MemberExpr*>(c->Callee.get());
            std::string modPath = ModulePathOf(m->Base.get());
            if (!modPath.empty()) {
                const ModuleInfo& mi = Mods[modPath];
                auto ff = mi.Fns.find(m->Name);
                if (ff != mi.Fns.end()) {
                    auto fd = Fns.find(ff->second);
                    if (fd != Fns.end()) return CheckUserCall(c, fd->second);
                }
                auto ft = mi.Types.find(m->Name);
                if (ft != mi.Types.end()) {
                    bool isGen = Generics.count(ft->second) > 0;
                    if (!c->TypeArgs.empty()) {
                        if (isGen) {
                            std::vector<Type> args;
                            bool bad = false;
                            for (auto& ta : c->TypeArgs) {
                                Type t = ResolveType(ta, false, false);
                                args.push_back(t);
                                bad |= IsErr(t);
                            }
                            if (!bad) {
                                std::string key = InstantiateGeneric(ft->second, args, c->Sp);
                                if (!key.empty()) return CheckStructInit(c, Structs[key]);
                            }
                            for (auto& a : c->Args) CheckExpr(a.Value.get());
                            return Type::Err();
                        }
                        Diag d = Rep.Err("E0313", c->Callee->Sp,
                            "`" + mi.Name + "." + m->Name +
                            "` no es un struct genérico: no lleva argumentos de tipo");
                        d.Helps.push_back("escribe `" + mi.Name + "." + m->Name + "(...)` sin corchetes");
                        Rep.Emit(d);
                        for (auto& a : c->Args) CheckExpr(a.Value.get());
                        return Type::Err();
                    }
                    if (isGen) {
                        const auto& params = Generics[ft->second].TD->TypeParams;
                        Diag d = Rep.Err("E0310", c->Callee->Sp,
                            "el struct `" + mi.Name + "." + m->Name +
                            "` es genérico: indica sus tipos entre corchetes");
                        d.Helps.push_back("ejemplo: " + mi.Name + "." + m->Name + "[" +
                                          (params.empty() ? std::string("int") : params[0]) +
                                          "](...)");
                        Rep.Emit(d);
                        for (auto& a : c->Args) CheckExpr(a.Value.get());
                        return Type::Err();
                    }
                    auto st = Structs.find(ft->second);
                    if (st != Structs.end()) return CheckStructInit(c, st->second);
                }
                Diag d = Rep.Err("E0302", m->NameSp,
                    "`" + mi.Name + "` no exporta nada llamado `" + m->Name + "`");
                std::string list;
                for (auto& kv : mi.Fns) list += (list.empty() ? "" : ", ") + kv.first;
                for (auto& kv : mi.Types) list += (list.empty() ? "" : ", ") + kv.first;
                if (!list.empty()) d.Helps.push_back("exporta: " + list);
                std::string best;
                int bd = 1 << 30;
                for (auto& kv : mi.Fns) {
                    if (!CloseEnough(m->Name, kv.first)) continue;
                    int dd = Levenshtein(m->Name, kv.first);
                    if (dd < bd) { bd = dd; best = kv.first; }
                }
                if (!best.empty())
                    d.Helps.push_back("¿quisiste decir `" + mi.Name + "." + best + "`?");
                Rep.Emit(d);
                for (auto& a : c->Args) CheckExpr(a.Value.get());
                return Type::Err();
            }
            if (m->Base->K == Expr::Kind::Ident) {
                auto* id = static_cast<IdentExpr*>(m->Base.get());
                auto mo = Mods.find(id->Name);
                if (mo != Mods.end()) return CheckModuleCall(c, m, mo->second);
                if (EnumInfo* ei = FindEnum(id->Name)) {
                    if (const EnumVarInfo* vv = FindVariantIn(*ei, m->Name)) {
                        VariantInfo vi{ei->Name, vv->Name, vv->Index, vv->Sp};
                        return CheckEnumCtor(c, vi);
                    }
                    Diag d = Rep.Err("E0602", m->NameSp,
                        "el enum `" + DisplayName(ei->Name) + "` no tiene ninguna variante `" +
                        m->Name + "`");
                    std::string list;
                    for (auto& v : ei->Variants) list += (list.empty() ? "" : ", ") + v.Name;
                    if (!list.empty()) d.Helps.push_back("sus variantes son: " + list);
                    Rep.Emit(d);
                    for (auto& a : c->Args) CheckExpr(a.Value.get());
                    return Type::Err();
                }
            }
            Type base = CheckExpr(m->Base.get());
            if (IsErr(base)) return Type::Err();
            Type pointee = base.Pointee();
            if (pointee.IsStruct()) {
                auto f = Structs.find(pointee.Name);
                if (f == Structs.end()) return Type::Err();
                auto fm = f->second.Methods.find(m->Name);
                if (fm == f->second.Methods.end()) {
                    Diag d = Rep.Err("E0304", m->NameSp,
                        "`" + f->second.Name + "` no tiene ningún método `" + m->Name + "`");
                    std::string list;
                    for (auto& kv : f->second.Methods)
                        list += (list.empty() ? "" : ", ") + kv.first;
                    if (!list.empty()) d.Helps.push_back("sus métodos son: " + list);
                    std::string best;
                    int bd = 1 << 30;
                    for (auto& kv : f->second.Methods) {
                        if (!CloseEnough(m->Name, kv.first)) continue;
                        int dd = Levenshtein(m->Name, kv.first);
                        if (dd < bd) { bd = dd; best = kv.first; }
                    }
                    if (!best.empty()) d.Helps.push_back("¿quisiste decir `" + best + "`?");
                    Rep.Emit(d);
                    for (auto& a : c->Args) CheckExpr(a.Value.get());
                    return Type::Err();
                }
                return CheckMethodCall(c, m, fm->second);
            }
            if (pointee.IsArray()) return CheckContainerMethod(c, m, pointee, true);
            if (pointee.IsMap()) return CheckContainerMethod(c, m, pointee, false);
            if (pointee.IsStr()) return CheckStringMethod(c, m);
            Diag d = Rep.Err("E0304", m->Base->Sp,
                "`" + TypeName(base) + "` no tiene métodos");
            Rep.Emit(d);
            return Type::Err();
        }
        Rep.Emit(Rep.Err("E0201", c->Callee->Sp,
            "solo se llaman funciones, métodos y structs"));
        return Type::Err();
    }
    Type CheckGenericCall(CallExpr* c, const std::string& name, const std::string& spelling) {
        std::string gk;
        bool known = false;
        if (name.find('.') != std::string::npos) {
            size_t dot = name.find('.');
            auto mo = Mods.find(name.substr(0, dot));
            if (mo != Mods.end()) {
                known = true;
                auto st = mo->second.Types.find(name.substr(dot + 1));
                if (st != mo->second.Types.end()) {
                    known = true;
                    if (Generics.count(st->second)) gk = st->second;
                }
            }
        } else {
            gk = FindGenericKey(name);
            known = gk.empty() &&
                    (FindStruct(name) != nullptr || FindEnum(name) != nullptr ||
                     FindFn(name) != nullptr || IsBuiltin(name));
        }
        if (gk.empty()) {
            if (!known) {
                Rep.Emit(UnknownName("E0302", c->Callee->Sp, "la función", spelling));
            } else {
                Diag d = Rep.Err("E0313", c->Callee->Sp,
                    "`" + spelling + "` no es un struct genérico: no lleva argumentos de tipo");
                d.Helps.push_back("los corchetes con tipos solo valen en structs declarados "
                                  "con `type Nombre[T]`");
                Rep.Emit(d);
            }
            for (auto& a : c->Args) CheckExpr(a.Value.get());
            return Type::Err();
        }
        std::vector<Type> args;
        bool bad = false;
        for (auto& ta : c->TypeArgs) {
            Type t = ResolveType(ta, false, false);
            args.push_back(t);
            bad |= IsErr(t);
        }
        if (bad) {
            for (auto& a : c->Args) CheckExpr(a.Value.get());
            return Type::Err();
        }
        std::string key = InstantiateGeneric(gk, args, c->Callee->Sp);
        if (key.empty()) {
            for (auto& a : c->Args) CheckExpr(a.Value.get());
            return Type::Err();
        }
        return CheckStructInit(c, Structs[key]);
    }
    bool CheckArgCount(CallExpr* c, const std::string& name, int mn, int mx) {
        int n = (int)c->Args.size();
        if (n >= mn && n <= mx) return true;
        Diag d;
        if (mn == mx) {
            d = Rep.Err("E0401", c->Sp,
                "`" + name + "` recibe " + std::to_string(mn) +
                (mn == 1 ? " argumento" : " argumentos") + ", pero le llegan " +
                std::to_string(n));
        } else {
            d = Rep.Err("E0401", c->Sp,
                "`" + name + "` recibe entre " + std::to_string(mn) + " y " +
                std::to_string(mx) + " argumentos, pero le llegan " + std::to_string(n));
        }
        if (name == "Print" && n > 1)
            d.Helps.push_back("junta los valores con interpolación: Print(\"{A} {B}\")");
        Rep.Emit(d);
        return false;
    }
    Type CheckHiloLanzar(CallExpr* c) {
        auto revisar_resto = [&](FnDecl* fd) {
            std::vector<Param*> ps;
            if (fd)
                for (auto& p : fd->Params)
                    if (!p.IsSelf) ps.push_back(&p);
            for (size_t i = 1; i < c->Args.size(); i++) CheckExpr(c->Args[i].Value.get());
            (void)ps;
        };
        if (c->Args[0].Value->K != Expr::Kind::Ident) {
            Diag d = Rep.Err("E0401", c->Args[0].Value->Sp,
                "HiloLanzar necesita el nombre de una función como primer argumento");
            d.Helps.push_back("ejemplo: HiloLanzar(Atender, Cliente, Direccion)");
            Rep.Emit(d);
            revisar_resto(nullptr);
            return Type::Err();
        }
        auto* id = static_cast<IdentExpr*>(c->Args[0].Value.get());
        FnDecl* fd = FindFn(id->Name);
        if (!fd || fd->Foreign || fd->IsMethod) {
            Diag d = Rep.Err("E0401", id->Sp,
                "no puedo lanzar `" + id->Name + "` en un hilo");
            d.Helps.push_back("HiloLanzar lanza funciones normales del programa: fn Nombre(...)");
            d.Helps.push_back("ejemplo: HiloLanzar(Atender, Cliente, Direccion)");
            Rep.Emit(d);
            revisar_resto(nullptr);
            return Type::Err();
        }
        auto TipoVale = [](const Type& t) {
            return t.IsInt() || t.IsFloat() || t.IsBool() || t.IsStr();
        };
        std::vector<Param*> ps;
        for (auto& p : fd->Params)
            if (!p.IsSelf) ps.push_back(&p);
        bool ok = true;
        for (auto* p : ps) {
            Type pt = ResolveType(p->Ty, false, true);
            if (!IsErr(pt) && !TipoVale(pt)) {
                Diag d = Rep.Err("E0401", p->Sp,
                    "no puedo lanzar `" + fd->Name + "` en un hilo: el parámetro `" + p->Name +
                        "` es `" + TypeName(pt) + "`");
                d.Helps.push_back(
                    "los parámetros de una función lanzada deben ser int, float, bool o string");
                Rep.Emit(d);
                ok = false;
            }
        }
        if (c->Args.size() - 1 != ps.size()) {
            Diag d = Rep.Err("E0401", c->Sp,
                "`" + fd->Name + "` recibe " + std::to_string(ps.size()) +
                    " argumentos, pero HiloLanzar le pasa " +
                    std::to_string(c->Args.size() - 1));
            d.Helps.push_back("ejemplo: HiloLanzar(Atender, Cliente, Direccion)");
            Rep.Emit(d);
            ok = false;
        }
        c->ArgCastFloat.assign(c->Args.size(), false);
        for (size_t i = 1; i < c->Args.size(); i++) {
            Type at = CheckExpr(c->Args[i].Value.get());
            if (IsErr(at)) continue;
            if (!TipoVale(at)) {
                Diag d = Rep.Err("E0401", c->Args[i].Value->Sp,
                    "HiloLanzar copia los argumentos al hilo: usa int, float, bool o string, "
                    "no `" + TypeName(at) + "`");
                Rep.Emit(d);
                ok = false;
                continue;
            }
            if (i - 1 < ps.size()) {
                Type pt = ResolveType(ps[i - 1]->Ty, false, true);
                if (!IsErr(pt)) {
                    if (pt.IsFloat() && at.IsInt()) {
                        c->ArgCastFloat[i] = true;
                    } else if (!pt.SameAs(at)) {
                        Diag d = Rep.Err("E0401", c->Args[i].Value->Sp,
                            "el argumento " + std::to_string(i) + " de `" + fd->Name +
                                "` debe ser `" + TypeName(pt) + "`, pero es `" +
                                TypeName(at) + "`");
                        Rep.Emit(d);
                        ok = false;
                    }
                }
            }
        }
        if (ok) c->SpawnTarget = FnKey(fd);
        return Type::Int();
    }
    Type CheckBuiltinCall(CallExpr* c, const std::string& name) {
        c->Resolved = CallExpr::Target::Builtin;
        c->TargetName = name;
        for (auto& b : kBuiltinNames) {
            if (b.Name == name) {
                if (!CheckArgCount(c, name, b.MinArgs, b.MaxArgs)) return Type::Err();
                break;
            }
        }
        c->ArgCastFloat.assign(c->Args.size(), false);
        if (name == "Print") {
            if (c->Args.size() == 1) CheckExpr(c->Args[0].Value.get());
            return Type::Void();
        }
        if (name == "Afirma") {
            Type ct = CheckExpr(c->Args[0].Value.get());
            if (!IsErr(ct) && !ct.IsBool()) {
                Diag d = Rep.Err("E0404", c->Args[0].Value->Sp,
                    "Afirma necesita una condición `bool`, pero recibe `" + TypeName(ct) + "`");
                d.Helps.push_back("ejemplo: Afirma(Doble(2) == 4, \"Doble(2) no es 4\")");
                Rep.Emit(d);
            }
            Type mt = CheckExpr(c->Args[1].Value.get());
            if (!IsErr(mt) && !mt.IsStr()) {
                Diag d = Rep.Err("E0401", c->Args[1].Value->Sp,
                    "el mensaje de Afirma debe ser `string`, pero es `" + TypeName(mt) + "`");
                Rep.Emit(d);
            }
            return Type::Void();
        }
        if (name == "Str") {
            CheckExpr(c->Args[0].Value.get());
            if (c->Args.size() == 2) {
                Type d = CheckExpr(c->Args[1].Value.get());
                if (!IsErr(d) && !d.IsInt()) {
                    Diag d2 = Rep.Err("E0401", c->Args[1].Value->Sp,
                        "los decimales de Str deben ser `int`, pero son `" +
                        TypeName(d) + "`");
                    d2.Helps.push_back(
                        "ejemplo: Str(3.14159, 2) devuelve \"3.14\"");
                    Rep.Emit(d2);
                }
            }
            return Type::Str();
        }
        if (name == "HiloLanzar") return CheckHiloLanzar(c);
        if (name == "TiempoMs") return Type::Float();
        if (name == "Sqrt") {
            Type t = CheckExpr(c->Args[0].Value.get());
            if (!IsErr(t) && !t.IsNumeric()) {
                Diag d = Rep.Err("E0401", c->Args[0].Value->Sp,
                    "Sqrt necesita un número, pero recibe `" + TypeName(t) + "`");
                Rep.Emit(d);
            }
            c->ArgCastFloat[0] = t.IsInt();
            return Type::Float();
        }
        if (name == "Abs") {
            Type t = CheckExpr(c->Args[0].Value.get());
            if (!IsErr(t) && !t.IsNumeric()) {
                Diag d = Rep.Err("E0401", c->Args[0].Value->Sp,
                    "Abs necesita un número, pero recibe `" + TypeName(t) + "`");
                Rep.Emit(d);
            }
            return t;
        }
        if (name == "Floor") {
            Type t = CheckExpr(c->Args[0].Value.get());
            if (!IsErr(t) && !t.IsNumeric()) {
                Diag d = Rep.Err("E0401", c->Args[0].Value->Sp,
                    "Floor necesita un número, pero recibe `" + TypeName(t) + "`");
                Rep.Emit(d);
            }
            c->ArgCastFloat[0] = t.IsInt();
            return Type::Int();
        }
        if (name == "Min" || name == "Max") {
            Type a = CheckExpr(c->Args[0].Value.get());
            Type b = CheckExpr(c->Args[1].Value.get());
            if (!IsErr(a) && !IsErr(b) && !(a.IsNumeric() && b.IsNumeric())) {
                Diag d = Rep.Err("E0401", c->Sp,
                    "`" + name + "` compara números, pero recibe `" + TypeName(a) +
                    "` y `" + TypeName(b) + "`");
                Rep.Emit(d);
                return Type::Err();
            }
            if (a.IsFloat() && b.IsInt()) c->ArgCastFloat[1] = true;
            if (a.IsInt() && b.IsFloat()) c->ArgCastFloat[0] = true;
            if (a.IsFloat() || b.IsFloat()) return Type::Float();
            return Type::Int();
        }
        if (name == "Pow") {
            Type a = CheckExpr(c->Args[0].Value.get());
            Type b = CheckExpr(c->Args[1].Value.get());
            if (!IsErr(a) && !IsErr(b) && !(a.IsNumeric() && b.IsNumeric())) {
                Diag d = Rep.Err("E0401", c->Sp, "Pow necesita números");
                Rep.Emit(d);
                return Type::Err();
            }
            c->ArgCastFloat[0] = a.IsInt();
            c->ArgCastFloat[1] = b.IsInt();
            return Type::Float();
        }
        return CheckExpr(c->Args[0].Value.get());
    }
    Type CheckUserCall(CallExpr* c, FnDecl* fd) {
        c->Resolved = CallExpr::Target::Fn;
        c->TargetName = fd->Foreign ? fd->Name : FnKey(fd);
        std::string savedMod = CurModule;
        CurModule = fd->Module;
        Type ret = fd->HasRet ? ResolveType(fd->Ret, false, true) : Type::Void();
        CurModule = savedMod;
        std::vector<Param*> ps;
        for (auto& p : fd->Params)
            if (!p.IsSelf) ps.push_back(&p);
        for (auto& a : c->Args) {
            if (!a.Name.empty()) {
                Diag d = Rep.Err("E0201", a.Sp,
                    "los argumentos con nombre solo se usan al construir structs");
                d.Helps.push_back("en `" + fd->Name + "(...)` los argumentos van por posición");
                Rep.Emit(d);
            }
        }
        if ((int)ps.size() != (int)c->Args.size()) {
            Diag d = Rep.Err("E0401", c->Sp,
                "`" + fd->Name + "` recibe " + std::to_string(ps.size()) +
                (ps.size() == 1 ? " argumento" : " argumentos") + ", pero le llegan " +
                std::to_string(c->Args.size()));
            d.Helps.push_back("su firma es: `" + fd->Name + ParamsToString(fd->Params) + "`");
            Rep.Emit(d);
            for (auto& a : c->Args) CheckExpr(a.Value.get());
            return ret;
        }
        c->ArgCastFloat.assign(c->Args.size(), false);
        c->ArgMoved.assign(c->Args.size(), false);
        struct BorrowUse { std::string Var; bool Mut; Span Sp; };
        std::vector<BorrowUse> borrows;
        for (size_t i = 0; i < c->Args.size(); i++) {
            Param* p = ps[i];
            Arg& a = c->Args[i];
            std::string savedP = CurModule;
            CurModule = fd->Module;
            Type pty = ResolveType(p->Ty, true, false);
            CurModule = savedP;
            if (pty.IsBorrow()) {
                if (!a.Borrowed) {
                    Diag d = Rep.Err("E0202", a.Sp,
                        "el parámetro `" + p->Name + "` espera un préstamo");
                    d.Helps.push_back(std::string("pásalo con & en la llamada: ") +
                                      (pty.Mut ? "`" + fd->Name + "(&" + p->Name + ")` "
                                                 "(la variable debe ser `var`)"
                                               : "`" + fd->Name + "(&" + p->Name + ")`"));
                    Rep.Emit(d);
                    CheckExpr(a.Value.get());
                    continue;
                }
                if (a.Value->K != Expr::Kind::Ident) {
                    Rep.Emit(Rep.Err("E0202", a.Value->Sp,
                        "solo se presta una variable: &Nombre"));
                    CheckExpr(a.Value.get());
                    continue;
                }
                auto* id = static_cast<IdentExpr*>(a.Value.get());
                VarInfo* v = FindVar(id->Name);
                if (!v) {
                    Rep.Emit(UnknownName("E0301", id->Sp, "la variable", id->Name));
                    continue;
                }
                v->Used = true;
                if (v->Moved) {
                    Diag d = Rep.Err("E0501", id->Sp,
                        "no puedes prestar `" + id->Name + "` porque su valor ya se movió");
                    d.Notes.push_back({Diag::NoteKind::Note,
                        "el valor se movió en esta otra línea", v->MoveSp});
                    d.Helps.push_back("escribe `Clone(" + id->Name +
                                      ")` en el punto de movimiento si quieres conservar una copia");
                    Rep.Emit(d);
                    continue;
                }
                if (pty.Mut && !v->Mutable) {
                    ReportNotMutable(id->Sp, id->Name, *v);
                    continue;
                }
                borrows.push_back({id->Name, pty.Mut, a.Sp});
                continue;
            }
            if (a.Borrowed) {
                Diag d = Rep.Err("E0202", a.Sp,
                    "aquí no hace falta `&`: el parámetro `" + p->Name + "` es de tipo `" +
                    TypeName(pty) + "`");
                d.Helps.push_back("quita el & (los números, bools y strings se pasan por valor)");
                Rep.Emit(d);
            }
            Type at = CheckExpr(a.Value.get(), IsErr(pty) ? nullptr : &pty);
            if (!IsErr(at) && !IsErr(pty) && !AssignableTo(at, pty))
                TypeMismatch(a.Value->Sp, pty, at);
            if (!IsErr(at) && at.IsInt() && pty.IsFloat()) c->ArgCastFloat[i] = true;
            if (at.IsStruct() && a.Value->K == Expr::Kind::Ident)
                c->ArgMoved[i] = MoveVar(a.Value.get());
        }
        for (size_t i = 0; i < borrows.size(); i++) {
            for (size_t j = i + 1; j < borrows.size(); j++) {
                if (borrows[i].Var != borrows[j].Var) continue;
                if (!borrows[i].Mut && !borrows[j].Mut) continue;
                int mi = borrows[i].Mut ? (int)i : (int)j;
                int ot = borrows[i].Mut ? (int)j : (int)i;
                Diag d = Rep.Err("E0503", borrows[mi].Sp,
                    "préstamos conflictivos: `" + borrows[i].Var +
                    "` se presta como mutable y también se usa en la misma llamada");
                d.Notes.push_back({Diag::NoteKind::Note, "y también se usa aquí", borrows[ot].Sp});
                d.Helps.push_back("hazlo en dos pasos: primero la llamada con &mut y después la otra");
                Rep.Emit(d);
            }
        }
        return ret;
    }
    std::string NamedInitHint(StructInfo& si) {
        std::string s;
        for (auto& f : si.Fields) {
            if (!s.empty()) s += ", ";
            s += f.Name + ": ...";
        }
        return s;
    }
    Type CheckStructInit(CallExpr* c, StructInfo& si) {
        c->Resolved = CallExpr::Target::StructInit;
        c->TargetName = si.Name;
        bool anyNamed = false, anyPos = false;
        for (auto& a : c->Args) {
            if (!a.Name.empty()) anyNamed = true; else anyPos = true;
        }
        if (anyNamed && anyPos) {
            Diag d = Rep.Err("E0201", c->Sp,
                "no se mezclan argumentos con nombre y sin nombre");
            d.Helps.push_back("la forma clara es: " + si.Name + "(" + NamedInitHint(si) + ")");
            Rep.Emit(d);
            for (auto& a : c->Args) CheckExpr(a.Value.get());
            return Type::Err();
        }
        if (anyPos) {
            if ((int)c->Args.size() != (int)si.Fields.size()) {
                Diag d = Rep.Err("E0401", c->Sp,
                    "`" + si.Name + "` tiene " + std::to_string(si.Fields.size()) +
                    " campos, pero le pasas " + std::to_string(c->Args.size()));
                std::string list;
                for (auto& f : si.Fields) list += (list.empty() ? "" : ", ") + f.Name;
                d.Helps.push_back("sus campos son: " + list);
                d.Helps.push_back("queda más claro con nombres: " + si.Name + "(" +
                                  NamedInitHint(si) + ")");
                Rep.Emit(d);
                for (auto& a : c->Args) CheckExpr(a.Value.get());
                return Type::Err();
            }
            for (size_t i = 0; i < c->Args.size(); i++) {
                Type at = CheckExpr(c->Args[i].Value.get());
                Type want = si.Fields[i].Ty;
                if (!IsErr(at) && !IsErr(want) && !AssignableTo(at, want))
                    TypeMismatch(c->Args[i].Value->Sp, want, at);
            }
            return Type::StructOf(si.Name);
        }
        std::unordered_set<std::string> given;
        for (auto& a : c->Args) {
            if (given.count(a.Name)) {
                Diag d = Rep.Err("E0305", a.Sp, "el campo `" + a.Name + "` aparece dos veces");
                Rep.Emit(d);
                continue;
            }
            given.insert(a.Name);
            auto f = std::find_if(si.Fields.begin(), si.Fields.end(),
                                  [&](const FieldInfo& fi) { return fi.Name == a.Name; });
            if (f == si.Fields.end()) {
                Diag d = Rep.Err("E0304", a.Sp,
                    "`" + si.Name + "` no tiene ningún campo `" + a.Name + "`");
                std::string list;
                for (auto& fi : si.Fields) list += (list.empty() ? "" : ", ") + fi.Name;
                if (!list.empty()) d.Helps.push_back("sus campos son: " + list);
                std::string best;
                int bd = 1 << 30;
                for (auto& fi : si.Fields) {
                    if (!CloseEnough(a.Name, fi.Name)) continue;
                    int dd = Levenshtein(a.Name, fi.Name);
                    if (dd < bd) { bd = dd; best = fi.Name; }
                }
                if (!best.empty()) d.Helps.push_back("¿quisiste decir `" + best + "`?");
                Rep.Emit(d);
                CheckExpr(a.Value.get());
                continue;
            }
            Type at = CheckExpr(a.Value.get());
            if (!IsErr(at) && !IsErr(f->Ty) && !AssignableTo(at, f->Ty))
                TypeMismatch(a.Value->Sp, f->Ty, at);
        }
        for (auto& fi : si.Fields) {
            if (given.count(fi.Name) || fi.HasDefault) continue;
            Diag d = Rep.Err("E0403", c->Sp,
                "falta el campo `" + fi.Name + "` en `" + si.Name + "(...)`");
            d.Notes.push_back({Diag::NoteKind::Note, "campo declarado aquí", fi.Sp});
            d.Helps.push_back("añádelo: " + si.Name + "(" + NamedInitHint(si) + ")");
            d.Helps.push_back("regla ES.20 de las C++ Core Guidelines: inicializa siempre un objeto");
            Rep.Emit(d);
        }
        return Type::StructOf(si.Name);
    }
    Type CheckMethodCall(CallExpr* c, MemberExpr* m, FnDecl* fd) {
        c->Resolved = CallExpr::Target::Method;
        c->TargetName = fd->Name;
        bool selfMut = !fd->Params.empty() && fd->Params[0].IsSelf && fd->Params[0].Mut;
        if (selfMut) CheckMutableTarget(m->Base.get());
        std::vector<Param*> ps;
        for (auto& p : fd->Params)
            if (!p.IsSelf) ps.push_back(&p);
        Type ret = fd->HasRet ? [&] { std::string s = CurModule; CurModule = fd->Module;
                                      Type t = ResolveType(fd->Ret, false, true);
                                      CurModule = s; return t; }() : Type::Void();
        if ((int)ps.size() != (int)c->Args.size()) {
            Diag d = Rep.Err("E0401", c->Sp,
                "`" + fd->Name + "` recibe " + std::to_string(ps.size()) +
                (ps.size() == 1 ? " argumento" : " argumentos") + ", pero le llegan " +
                std::to_string(c->Args.size()));
            d.Helps.push_back("su firma es: `" + fd->Name + ParamsToString(fd->Params) + "`");
            Rep.Emit(d);
            for (auto& a : c->Args) CheckExpr(a.Value.get());
            return ret;
        }
        c->ArgCastFloat.assign(c->Args.size(), false);
        c->ArgMoved.assign(c->Args.size(), false);
        for (size_t i = 0; i < c->Args.size(); i++) {
            Arg& a = c->Args[i];
            Param* p = ps[i];
            if (!a.Name.empty()) {
                Diag d = Rep.Err("E0201", a.Sp,
                    "los argumentos con nombre solo se usan al construir structs");
                Rep.Emit(d);
            }
            std::string savedP2 = CurModule;
            CurModule = fd->Module;
            Type pty = ResolveType(p->Ty, true, false);
            CurModule = savedP2;
            if (pty.IsBorrow()) {
                if (!a.Borrowed) {
                    Diag d = Rep.Err("E0202", a.Sp,
                        "el parámetro `" + p->Name + "` espera un préstamo: pásalo con &");
                    Rep.Emit(d);
                    CheckExpr(a.Value.get());
                    continue;
                }
                if (a.Value->K != Expr::Kind::Ident) {
                    Rep.Emit(Rep.Err("E0202", a.Value->Sp,
                        "solo se presta una variable: &Nombre"));
                    continue;
                }
                auto* id = static_cast<IdentExpr*>(a.Value.get());
                if (!UseVar(a.Value.get())) continue;
                VarInfo* v = FindVar(id->Name);
                if (v && pty.Mut && !v->Mutable) ReportNotMutable(id->Sp, id->Name, *v);
                continue;
            }
            if (a.Borrowed) {
                Diag d = Rep.Err("E0202", a.Sp,
                    "aquí no hace falta `&`: el parámetro `" + p->Name + "` es de tipo `" +
                    TypeName(pty) + "`");
                Rep.Emit(d);
            }
            Type at = CheckExpr(a.Value.get(), IsErr(pty) ? nullptr : &pty);
            if (!IsErr(at) && !IsErr(pty) && !AssignableTo(at, pty))
                TypeMismatch(a.Value->Sp, pty, at);
            if (!IsErr(at) && at.IsInt() && pty.IsFloat()) c->ArgCastFloat[i] = true;
            if (at.IsStruct() && a.Value->K == Expr::Kind::Ident)
                c->ArgMoved[i] = MoveVar(a.Value.get());
        }
        return ret;
    }
    Type CheckContainerMethod(CallExpr* c, MemberExpr* m, Type container, bool isArray) {
        c->Resolved = CallExpr::Target::Method;
        c->TargetName = m->Name;
        const std::string& n = m->Name;
        auto err = [&](const std::string& msg, const std::string& help) {
            Diag d = Rep.Err("E0304", m->NameSp, msg);
            if (!help.empty()) d.Helps.push_back(help);
            Rep.Emit(d);
            for (auto& a : c->Args) CheckExpr(a.Value.get());
            return Type::Err();
        };
        if (isArray) {
            if (n == "Push") {
                if ((int)c->Args.size() != 1)
                    return err("Push recibe 1 argumento (el valor a añadir)", "");
                Type vt = CheckExpr(c->Args[0].Value.get());
                Type want = container.Sub[0];
                if (!IsErr(vt) && !IsErr(want) && !AssignableTo(vt, want))
                    TypeMismatch(c->Args[0].Value->Sp, want, vt);
                CheckMutableTarget(m->Base.get());
                c->ArgCastFloat.assign(1, !IsErr(want) && vt.IsInt() && want.IsFloat());
                c->ArgMoved.assign(1, false);
                if (!IsErr(want) && want.IsStruct())
                    c->ArgMoved[0] = MoveVar(c->Args[0].Value.get());
                return Type::Void();
            }
            if (n == "Pop") {
                if ((int)c->Args.size() != 0) return err("Pop no recibe argumentos", "");
                CheckMutableTarget(m->Base.get());
                return container.Sub[0];
            }
            if (n == "Len") {
                if ((int)c->Args.size() != 0) return err("Len no recibe argumentos", "");
                return Type::Int();
            }
            if (n == "Contiene") {
                if ((int)c->Args.size() != 1) return err("Contiene recibe 1 argumento", "");
                Type vt = CheckExpr(c->Args[0].Value.get());
                if (!IsErr(vt) && !IsErr(container.Sub[0]) &&
                    !AssignableTo(vt, container.Sub[0]))
                    TypeMismatch(c->Args[0].Value->Sp, container.Sub[0], vt);
                return Type::Bool();
            }
            return err("`" + n + "` no es un método de arrays",
                       "métodos de arrays: Push, Pop, Len, Contiene");
        }
        if (n == "Len") {
            if ((int)c->Args.size() != 0) return err("Len no recibe argumentos", "");
            return Type::Int();
        }
        if (n == "Contiene") {
            if ((int)c->Args.size() != 1) return err("Contiene recibe 1 argumento", "");
            Type kt = CheckExpr(c->Args[0].Value.get());
            if (!IsErr(kt) && !IsErr(container.Sub[0]) && !AssignableTo(kt, container.Sub[0]))
                TypeMismatch(c->Args[0].Value->Sp, container.Sub[0], kt);
            return Type::Bool();
        }
        if (n == "Claves") {
            if ((int)c->Args.size() != 0) return err("Claves no recibe argumentos", "");
            return Type::ArrayOf(container.Sub[0]);
        }
        return err("`" + n + "` no es un método de mapas",
                   "métodos de mapas: Len, Contiene, Claves");
    }
    Type CheckStringMethod(CallExpr* c, MemberExpr* m) {
        c->Resolved = CallExpr::Target::Method;
        c->TargetName = m->Name;
        const std::string& n = m->Name;
        if (n == "Len") {
            if ((int)c->Args.size() != 0) {
                Diag d = Rep.Err("E0401", c->Sp, "Len no recibe argumentos");
                Rep.Emit(d);
            }
            return Type::Int();
        }
        if (n == "Contiene") {
            if ((int)c->Args.size() != 1) {
                Diag d = Rep.Err("E0401", c->Sp, "Contiene recibe 1 argumento");
                Rep.Emit(d);
                return Type::Err();
            }
            Type t = CheckExpr(c->Args[0].Value.get());
            if (!IsErr(t) && !t.IsStr()) {
                Diag d = Rep.Err("E0401", c->Args[0].Value->Sp,
                    "Contiene busca un string, pero recibe `" + TypeName(t) + "`");
                Rep.Emit(d);
            }
            return Type::Bool();
        }
        Diag d = Rep.Err("E0304", m->NameSp, "`" + n + "` no es un método de strings");
        d.Helps.push_back("métodos de strings: Len, Contiene");
        Rep.Emit(d);
        return Type::Err();
    }
    Type CheckEnumCtor(CallExpr* c, const VariantInfo& vi) {
        c->Resolved = CallExpr::Target::EnumCtor;
        c->TargetName = vi.EnumName;
        c->VariantName = vi.VariantName;
        Type et = Type::EnumOf(vi.EnumName);
        auto ef = Enums.find(vi.EnumName);
        const EnumVarInfo* vv =
            (ef != Enums.end()) ? FindVariantIn(ef->second, vi.VariantName) : nullptr;
        for (auto& a : c->Args) {
            if (!a.Name.empty()) {
                Diag d = Rep.Err("E0201", a.Sp,
                    "los valores de un enum se construyen por posición");
                d.Helps.push_back("escribe `" + vi.VariantName + "(valor1, valor2)` sin nombres");
                Rep.Emit(d);
            }
        }
        if (!vv) return et;
        if ((int)c->Args.size() != (int)vv->Fields.size()) {
            Diag d = Rep.Err("E0401", c->Sp,
                "la variante `" + vi.VariantName + "` lleva " +
                std::to_string(vv->Fields.size()) +
                (vv->Fields.size() == 1 ? " valor" : " valores") + ", pero le pasas " +
                std::to_string(c->Args.size()));
            std::string list;
            for (auto& f : vv->Fields) list += (list.empty() ? "" : ", ") + f.Name;
            if (!list.empty()) d.Helps.push_back("sus campos son: " + list);
            Rep.Emit(d);
            for (auto& a : c->Args) CheckExpr(a.Value.get());
            return et;
        }
        c->ArgCastFloat.assign(c->Args.size(), false);
        for (size_t i = 0; i < c->Args.size(); i++) {
            Type want = vv->Fields[i].Ty;
            Type at = CheckExpr(c->Args[i].Value.get(), IsErr(want) ? nullptr : &want);
            if (!IsErr(at) && !IsErr(want) && !AssignableTo(at, want))
                TypeMismatch(c->Args[i].Value->Sp, want, at);
            if (!IsErr(at) && at.IsInt() && want.IsFloat()) c->ArgCastFloat[i] = true;
            if (at.IsStruct() && c->Args[i].Value->K == Expr::Kind::Ident) {
                c->ArgMoved.assign(c->Args.size(), false);
                c->ArgMoved[i] = MoveVar(c->Args[i].Value.get());
            }
        }
        return et;
    }
    Type CheckModuleCall(CallExpr* c, MemberExpr* m, const ModuleInfo& mi) {
        auto ff = mi.Fns.find(m->Name);
        if (ff != mi.Fns.end()) {
            auto fd = Fns.find(ff->second);
            if (fd != Fns.end()) return CheckUserCall(c, fd->second);
        }
        auto ft = mi.Types.find(m->Name);
        if (ft != mi.Types.end()) {
            bool isGen = Generics.count(ft->second) > 0;
            if (!c->TypeArgs.empty()) {
                if (isGen) {
                    std::vector<Type> args;
                    bool bad = false;
                    for (auto& ta : c->TypeArgs) {
                        Type t = ResolveType(ta, false, false);
                        args.push_back(t);
                        bad |= IsErr(t);
                    }
                    if (!bad) {
                        std::string key = InstantiateGeneric(ft->second, args, c->Sp);
                        if (!key.empty()) return CheckStructInit(c, Structs[key]);
                    }
                    for (auto& a : c->Args) CheckExpr(a.Value.get());
                    return Type::Err();
                }
                Diag d = Rep.Err("E0313", c->Callee->Sp,
                    "`" + mi.Name + "." + m->Name +
                    "` no es un struct genérico: no lleva argumentos de tipo");
                d.Helps.push_back("escribe `" + mi.Name + "." + m->Name + " sin corchetes");
                Rep.Emit(d);
                for (auto& a : c->Args) CheckExpr(a.Value.get());
                return Type::Err();
            }
            if (isGen) {
                const auto& params = Generics[ft->second].TD->TypeParams;
                Diag d = Rep.Err("E0310", c->Callee->Sp,
                    "el struct `" + mi.Name + "." + m->Name +
                    "` es genérico: indica sus tipos entre corchetes");
                d.Helps.push_back("ejemplo: " + mi.Name + "." + m->Name + "[" +
                                  (params.empty() ? std::string("int") : params[0]) + "](...)");
                Rep.Emit(d);
                for (auto& a : c->Args) CheckExpr(a.Value.get());
                return Type::Err();
            }
            auto st = Structs.find(ft->second);
            if (st != Structs.end()) return CheckStructInit(c, st->second);
        }
        Diag d = Rep.Err("E0302", m->NameSp,
            "`" + mi.Name + "` no exporta nada llamado `" + m->Name + "`");
        std::string list;
        for (auto& kv : mi.Fns) list += (list.empty() ? "" : ", ") + kv.first;
        for (auto& kv : mi.Types) list += (list.empty() ? "" : ", ") + kv.first;
        if (!list.empty()) d.Helps.push_back("exporta: " + list);
        std::string best;
        int bd = 1 << 30;
        for (auto& kv : mi.Fns) {
            if (!CloseEnough(m->Name, kv.first)) continue;
            int dd = Levenshtein(m->Name, kv.first);
            if (dd < bd) { bd = dd; best = kv.first; }
        }
        if (!best.empty()) d.Helps.push_back("¿quisiste decir `" + mi.Name + "." + best + "`?");
        Rep.Emit(d);
        for (auto& a : c->Args) CheckExpr(a.Value.get());
        return Type::Err();
    }
    void CheckMatch(MatchStmt* s) {
        Type st = CheckExpr(s->Subject.get());
        const EnumInfo* ei = nullptr;
        bool enumSubject = st.IsEnum();
        if (enumSubject) {
            auto f = Enums.find(st.Name);
            if (f != Enums.end()) ei = &f->second;
        }
        std::unordered_map<std::string, Span> covered;
        bool sawWildcard = false;
        MoveSnap acum = Scopes;
        for (auto& arm : s->Arms) {
            Pattern& pat = arm.Pat;
            if (pat.k == Pattern::K::Wildcard) {
                sawWildcard = true;
                WalkBranch(arm.Body, acum);
                continue;
            }
            if (pat.k == Pattern::K::Literal) {
                Type lt = CheckExpr(pat.Lit.get());
                if (enumSubject && !IsErr(st)) {
                    Diag d = Rep.Err("E0602", pat.Sp,
                        "el sujeto del match es el enum `" + TypeName(st) +
                        "`: sus casos son variantes, no literales");
                    d.Helps.push_back("usa `case NombreVariante:` (con `(X, Y)` si lleva valores)");
                    Rep.Emit(d);
                    WalkBranch(arm.Body, acum);
                    continue;
                }
                if (!IsErr(st) && !IsErr(lt) && !AssignableTo(lt, st) && !AssignableTo(st, lt))
                    TypeMismatch(pat.Lit->Sp, st, lt);
                WalkBranch(arm.Body, acum);
                continue;
            }
            if (!enumSubject) {
                if (!IsErr(st)) {
                    Diag d = Rep.Err("E0602", pat.Sp,
                        "el patrón `" + pat.Name + "` es de enum, pero el sujeto del match es `" +
                        TypeName(st) + "`");
                    d.Helps.push_back("solo los enums tienen variantes; para otros valores "
                                      "usa literales o `case _:`");
                    Rep.Emit(d);
                }
                WalkBranch(arm.Body, acum);
                continue;
            }
            std::string enumKey, varName;
            if (pat.Qualified) {
                size_t dot = pat.Name.find('.');
                std::string enumDisplay = pat.Name.substr(0, dot);
                varName = pat.Name.substr(dot + 1);
                EnumInfo* e2 = FindEnum(enumDisplay);
                if (!e2) {
                    Diag d = Rep.Err("E0602", pat.Sp,
                        "el enum `" + enumDisplay + "` no existe aquí");
                    Span ssp;
                    std::string sug = SuggestName(enumDisplay, &ssp);
                    if (!sug.empty()) d.Helps.push_back("¿quisiste decir `" + sug + "`?");
                    Rep.Emit(d);
                    WalkBranch(arm.Body, acum);
                    continue;
                }
                enumKey = e2->Name;
            } else {
                VariantInfo* vf = FindVariant(pat.Name);
                if (!vf) {
                    Diag d = Rep.Err("E0602", pat.Sp,
                        "la variante `" + pat.Name + "` no existe");
                    std::string best;
                    int bd = 1 << 30;
                    for (auto& kv : Variantes) {
                        if (!CloseEnough(pat.Name, kv.first)) continue;
                        int dd = Levenshtein(pat.Name, kv.first);
                        if (dd < bd) { bd = dd; best = kv.first; }
                    }
                    if (!best.empty()) d.Helps.push_back("¿quisiste decir `" + best + "`?");
                    Rep.Emit(d);
                    WalkBranch(arm.Body, acum);
                    continue;
                }
                enumKey = vf->EnumName;
                varName = vf->VariantName;
            }
            auto ef = Enums.find(enumKey);
            const EnumVarInfo* vv =
                (ef != Enums.end()) ? FindVariantIn(ef->second, varName) : nullptr;
            if (!vv) {
                WalkBranch(arm.Body, acum);
                continue;
            }
            pat.TagIndex = vv->Index;
            pat.FieldNames.clear();
            for (auto& f : vv->Fields) pat.FieldNames.push_back(f.Name);
            if (!IsErr(st) && enumKey != st.Name) {
                Diag d = Rep.Err("E0602", pat.Sp,
                    "este patrón es del enum `" + DisplayName(enumKey) + "`, pero el sujeto del "
                    "match es `" + TypeName(st) + "`");
                Rep.Emit(d);
                WalkBranch(arm.Body, acum);
                continue;
            }
            auto dup = covered.find(varName);
            if (dup != covered.end()) {
                Diag d = Rep.Err("E0602", pat.Sp,
                    "la variante `" + varName + "` ya tiene su caso en este match");
                d.Notes.push_back(
                    {Diag::NoteKind::Note, "el primer caso estaba aquí", dup->second});
                d.Helps.push_back("borra uno de los dos casos: el primero que encaja gana");
                Rep.Emit(d);
            } else {
                covered[varName] = pat.Sp;
            }
            if (!pat.Bindings.empty() && pat.Bindings.size() != vv->Fields.size()) {
                Diag d = Rep.Err("E0602", pat.Sp,
                    "la variante `" + varName + "` lleva " +
                    std::to_string(vv->Fields.size()) +
                    (vv->Fields.size() == 1 ? " valor" : " valores") + ", pero enlazas " +
                    std::to_string(pat.Bindings.size()));
                std::string list;
                for (auto& f : vv->Fields) list += (list.empty() ? "" : ", ") + f.Name;
                if (!list.empty()) d.Helps.push_back("sus campos son: " + list +
                                                     " (usa `_` para ignorar alguno)");
                Rep.Emit(d);
            }
            MoveSnap savedArm = Scopes;
            size_t armDepth = Scopes.size();
            Push();
            for (size_t i = 0; i < pat.Bindings.size() && i < vv->Fields.size(); i++) {
                if (pat.Bindings[i] == "_") continue;
                VarInfo v;
                v.Mutable = false;
                v.Moveable = false;
                v.Ty = vv->Fields[i].Ty;
                v.DeclSp = pat.Sp;
                v.KindV = VarInfo::Kind::Let;
                Bind(pat.Bindings[i], std::move(v), pat.Sp);
            }
            WalkBlock(arm.Body);
            Pop();
            MergeMovesInto(acum, armDepth);
            Scopes = std::move(savedArm);
        }
        Scopes = std::move(acum);
        if (IsErr(st)) return;
        if (ei) {
            if (!sawWildcard) {
                std::string missing;
                for (auto& v : ei->Variants)
                    if (!covered.count(v.Name)) missing += (missing.empty() ? "" : ", ") + v.Name;
                if (!missing.empty()) {
                    Diag d = Rep.Err("E0601", s->Subject->Sp,
                        "este match no cubre todos los casos del enum `" +
                        DisplayName(st.Name) + "`\nfaltan: " + missing);
                    d.Helps.push_back("añade un `case` para cada variante, "
                                      "o un `case _:` al final como comodín");
                    d.Helps.push_back("Sylt es exhaustivo: si mañana añades una variante, "
                                      "el compilador te dirá qué match se quedó corto");
                    Rep.Emit(d);
                }
            }
        } else if (!sawWildcard) {
            Diag d = Rep.Err("E0601", s->Subject->Sp,
                "este match no cubre todos los valores posibles");
            d.Helps.push_back("para no-enums añade un `case _:` al final");
            Rep.Emit(d);
        }
    }
};
}
bool Check(ProgramP& prog, const SourceFile& src, Reporter& rep,
           const std::unordered_map<std::string, const SourceFile*>* files,
           bool requireMain) {
    Checker ck(src, rep, prog);
    ck.Files = files;
    ck.RequireMain = requireMain;
    ck.RegisterAll();
    if (rep.ErrorCount() == 0) ck.CheckBodies();
    if (rep.ErrorCount() == 0) ck.CheckInstantiations();
    if (rep.ErrorCount() == 0) {
        for (auto& td : ck.InstDecls) prog->Types.push_back(std::move(td));
        ck.InstDecls.clear();
    }
    return rep.ErrorCount() == 0;
}
}
