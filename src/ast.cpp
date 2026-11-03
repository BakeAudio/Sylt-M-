#include "ast.hpp"
#include <sstream>
namespace sylt {
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
std::string TypeName(const Type& t) {
    switch (t.K) {
        case TK::Int: return "int";
        case TK::Float: return "float";
        case TK::Bool: return "bool";
        case TK::Str: return "string";
        case TK::Void: return "void";
        case TK::Error: return "?";
        case TK::Array: return "[" + TypeName(t.Sub[0]) + "]";
        case TK::Map: return "{" + TypeName(t.Sub[0]) + ": " + TypeName(t.Sub[1]) + "}";
        case TK::Tuple: {
            std::string s = "(";
            for (size_t i = 0; i < t.Sub.size(); i++) {
                if (i) s += ", ";
                s += TypeName(t.Sub[i]);
            }
            return s + ")";
        }
        case TK::Struct: return DisplayName(t.Name);
        case TK::Enum: return DisplayName(t.Name);
        case TK::Borrow: return (t.Mut ? "&mut " : "&") + TypeName(t.Sub[0]);
    }
    return "?";
}
const char* ExprKindName(Expr::Kind k) {
    switch (k) {
        case Expr::Kind::IntLit: return "un número";
        case Expr::Kind::FloatLit: return "un número";
        case Expr::Kind::BoolLit: return "un bool";
        case Expr::Kind::StrLit: return "un string";
        case Expr::Kind::Ident: return "un nombre";
        case Expr::Kind::Unary: return "una expresión unaria";
        case Expr::Kind::Binary: return "una expresión";
        case Expr::Kind::Call: return "una llamada";
        case Expr::Kind::Index: return "una indexación";
        case Expr::Kind::Member: return "un acceso a campo";
        case Expr::Kind::ArrayLit: return "un array";
        case Expr::Kind::MapLit: return "un mapa";
        case Expr::Kind::TupleLit: return "una tupla";
    }
    return "una expresión";
}
std::string ParamsToString(const std::vector<Param>& ps) {
    std::ostringstream o;
    o << "(";
    bool first = true;
    for (auto& p : ps) {
        if (p.IsSelf) {
            o << (p.Mut ? "mut self" : "self");
        } else {
            if (!first) o << ", ";
            o << p.Name;
        }
        first = false;
    }
    o << ")";
    return o.str();
}
}
