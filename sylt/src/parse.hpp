#pragma once
#include <vector>
#include "ast.hpp"
#include "diag.hpp"
#include "lex.hpp"
namespace sylt {
ProgramP Parse(const std::vector<Token>& toks, const SourceFile& src, Reporter& rep);
ExprP ParseSubExpression(const std::vector<Token>& toks, const SourceFile& src, Reporter& rep);
}
