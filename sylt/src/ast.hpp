#pragma once
#include <memory>
#include <string>
#include <vector>
#include "common.hpp"
namespace sylt {
enum class TK { Int, Float, Bool, Str, Void, Array, Map, Tuple, Struct, Enum, Borrow, Error };
struct Type {
    TK K = TK::Error;
    std::vector<Type> Sub;
    std::string Name;
    bool Mut = false;
    Type() = default;
    explicit Type(TK k) : K(k) {}
    static Type Int() { return Type(TK::Int); }
    static Type Float() { return Type(TK::Float); }
    static Type Bool() { return Type(TK::Bool); }
    static Type Str() { return Type(TK::Str); }
    static Type Void() { return Type(TK::Void); }
    static Type Err() { return Type(TK::Error); }
    static Type ArrayOf(Type e) { Type t(TK::Array); t.Sub.push_back(e); return t; }
    static Type MapOf(Type k, Type v) { Type t(TK::Map); t.Sub.push_back(k); t.Sub.push_back(v); return t; }
    static Type TupleOf(std::vector<Type> ms) { Type t(TK::Tuple); t.Sub = std::move(ms); return t; }
    static Type StructOf(std::string n) { Type t(TK::Struct); t.Name = std::move(n); return t; }
    static Type EnumOf(std::string n) { Type t(TK::Enum); t.Name = std::move(n); return t; }
    static Type BorrowOf(Type inner, bool mut) {
        Type t(TK::Borrow);
        t.Sub.push_back(inner);
        t.Mut = mut;
        t.Name = inner.Name;
        return t;
    }
    bool IsError() const { return K == TK::Error; }
    bool IsInt() const { return K == TK::Int; }
    bool IsFloat() const { return K == TK::Float; }
    bool IsBool() const { return K == TK::Bool; }
    bool IsStr() const { return K == TK::Str; }
    bool IsVoid() const { return K == TK::Void; }
    bool IsArray() const { return K == TK::Array; }
    bool IsMap() const { return K == TK::Map; }
    bool IsTuple() const { return K == TK::Tuple; }
    bool IsStruct() const { return K == TK::Struct; }
    bool IsEnum() const { return K == TK::Enum; }
    bool IsBorrow() const { return K == TK::Borrow; }
    bool IsNumeric() const { return K == TK::Int || K == TK::Float; }
    Type Pointee() const {
        if (K == TK::Borrow && !Sub.empty()) return Sub[0];
        return *this;
    }
    bool SameAs(const Type& o) const {
        if (K != o.K || Name != o.Name || Mut != o.Mut) return false;
        if (Sub.size() != o.Sub.size()) return false;
        for (size_t i = 0; i < Sub.size(); i++)
            if (!Sub[i].SameAs(o.Sub[i])) return false;
        return true;
    }
};
std::string TypeName(const Type& t);
struct Expr;
struct Stmt;
struct Program;
using ExprP = std::unique_ptr<Expr>;
using StmtP = std::unique_ptr<Stmt>;
using ProgramP = std::unique_ptr<Program>;
struct TypeAnn;
struct Expr {
    enum class Kind {
        IntLit, FloatLit, BoolLit, StrLit, Ident,
        Unary, Binary,
        Call,
        Index, Member,
        ArrayLit, MapLit, TupleLit,
    };
    Kind K;
    Span Sp;
    Type Ty;
    virtual ~Expr() = default;
    explicit Expr(Kind k) : K(k) {}
};
struct IntLitExpr : Expr {
    int64_t Val = 0;
    IntLitExpr() : Expr(Kind::IntLit) {}
};
struct FloatLitExpr : Expr {
    double Val = 0;
    FloatLitExpr() : Expr(Kind::FloatLit) {}
};
struct BoolLitExpr : Expr {
    bool Val = false;
    BoolLitExpr() : Expr(Kind::BoolLit) {}
};
struct StrPart {
    bool IsText;
    std::string Text;
    ExprP Sub;
};
struct StrLitExpr : Expr {
    std::vector<StrPart> Parts;
    bool HasInterp = false;
    StrLitExpr() : Expr(Kind::StrLit) {}
};
struct IdentExpr : Expr {
    std::string Name;
    IdentExpr() : Expr(Kind::Ident) {}
};
struct UnaryExpr : Expr {
    std::string Op;
    ExprP Sub;
    UnaryExpr() : Expr(Kind::Unary) {}
};
struct BinaryExpr : Expr {
    std::string Op;
    ExprP L, R;
    BinaryExpr() : Expr(Kind::Binary) {}
};
struct Arg {
    std::string Name;
    bool Borrowed = false;
    ExprP Value;
    Span Sp;
};
struct CallExpr : Expr {
    ExprP Callee;
    std::vector<Arg> Args;
    enum class Target { None, Builtin, Fn, StructInit, Method, EnumCtor } Resolved = Target::None;
    std::string TargetName;
    std::string VariantName;
    std::vector<bool> ArgCastFloat;
    std::vector<bool> ArgMoved;
    std::vector<TypeAnn> TypeArgs;
    bool ReceiverMutable = false;
    std::string SpawnTarget;
    int SpawnSite = -1;
    CallExpr() : Expr(Kind::Call) {}
};
struct IndexExpr : Expr {
    ExprP Base, Idx;
    bool TupleIndex = false;
    int TuplePos = 0;
    IndexExpr() : Expr(Kind::Index) {}
};
struct MemberExpr : Expr {
    ExprP Base;
    std::string Name;
    Span NameSp;
    bool BuiltinMethod = false;
    bool IsEnumUnit = false;
    std::string EnumKey;
    MemberExpr() : Expr(Kind::Member) {}
};
struct ArrayLitExpr : Expr {
    std::vector<ExprP> Elems;
    std::vector<bool> ElemsMoved;
    ArrayLitExpr() : Expr(Kind::ArrayLit) {}
};
struct MapPair {
    ExprP Key, Val;
    Span Sp;
    bool ValMoved = false;
};
struct MapLitExpr : Expr {
    std::vector<MapPair> Pairs;
    MapLitExpr() : Expr(Kind::MapLit) {}
};
struct TupleLitExpr : Expr {
    std::vector<ExprP> Elems;
    TupleLitExpr() : Expr(Kind::TupleLit) {}
};
struct Stmt {
    enum class Kind { Let, Assign, Expr, Return, If, While, For, Break, Continue, Pass, Match };
    Kind K;
    Span Sp;
    virtual ~Stmt() = default;
    explicit Stmt(Kind k) : K(k) {}
};
struct Block {
    std::vector<StmtP> Stmts;
    Span Sp;
};
struct TypeAnn {
    enum class K { None, Prim, Ident, Generic, Array, Map, Tuple, Borrow };
    K k = K::None;
    std::string Name;
    bool Mut = false;
    std::vector<TypeAnn> Sub;
    bool HasResolvedTy = false;
    Type ResolvedTy;
    Span Sp;
};
struct Param {
    std::string Name;
    bool IsSelf = false;
    bool Mut = false;
    bool HasTy = false;
    TypeAnn Ty;
    Span Sp;
};
struct FnDecl {
    std::string Name;
    std::vector<Param> Params;
    bool HasRet = false;
    TypeAnn Ret;
    Block Body;
    Span Sp;
    bool IsMethod = false;
    bool SelfMut = false;
    std::string OwnerStruct;
    bool Foreign = false;
    std::string Module;
};
struct FieldDecl {
    std::string Name;
    TypeAnn Ty;
    bool HasDefault = false;
    ExprP Default;
    Span Sp;
};
struct TypeDecl {
    std::string Name;
    std::vector<std::string> TypeParams;
    std::vector<FieldDecl> Fields;
    std::vector<std::unique_ptr<FnDecl>> Methods;
    Span Sp;
    std::string Module;
};
struct LetStmt : Stmt {
    bool Mutable = false;
    std::vector<std::string> Names;
    bool Destructure = false;
    bool HasTy = false;
    TypeAnn Ty;
    ExprP Init;
    Type VarType;
    LetStmt() : Stmt(Kind::Let) {}
};
struct AssignStmt : Stmt {
    std::string Op;
    ExprP Target;
    ExprP Value;
    bool ValMoved = false;
    AssignStmt() : Stmt(Kind::Assign) {}
};
struct ExprStmt : Stmt {
    ExprP Sub;
    ExprStmt() : Stmt(Kind::Expr) {}
};
struct ReturnStmt : Stmt {
    ExprP Value;
    ReturnStmt() : Stmt(Kind::Return) {}
};
struct IfStmt : Stmt {
    ExprP Cond;
    Block Then;
    struct Branch { ExprP Cond; Block Body; };
    std::vector<Branch> Elifs;
    bool HasElse = false;
    Block Else;
    IfStmt() : Stmt(Kind::If) {}
};
struct WhileStmt : Stmt {
    ExprP Cond;
    Block Body;
    WhileStmt() : Stmt(Kind::While) {}
};
struct ForStmt : Stmt {
    std::string Var;
    bool IsRange = false;
    ExprP Low, High;
    Block Body;
    ForStmt() : Stmt(Kind::For) {}
};
struct EnumVariant {
    std::string Name;
    std::vector<FieldDecl> Fields;
    Span Sp;
};
struct EnumDecl {
    std::string Name;
    std::vector<EnumVariant> Variants;
    Span Sp;
    std::string Module;
};
struct ImportDecl {
    std::string Module;
    Span Sp;
};
struct Program {
    std::vector<ImportDecl> Imports;
    std::vector<std::string> ForeignIncludes;
    std::vector<std::unique_ptr<FnDecl>> Fns;
    std::vector<std::unique_ptr<TypeDecl>> Types;
    std::vector<std::unique_ptr<EnumDecl>> Enums;
    Span Sp;
};
struct Pattern {
    enum class K { Variant, Literal, Wildcard };
    K k = K::Wildcard;
    std::string Name;
    bool Qualified = false;
    std::vector<std::string> Bindings;
    ExprP Lit;
    Span Sp;
    int TagIndex = -1;
    std::vector<std::string> FieldNames;
};
struct MatchArm {
    Pattern Pat;
    Block Body;
};
struct MatchStmt : Stmt {
    ExprP Subject;
    std::vector<MatchArm> Arms;
    MatchStmt() : Stmt(Kind::Match) {}
};
const char* ExprKindName(Expr::Kind k);
std::string ParamsToString(const std::vector<Param>& ps);
}
