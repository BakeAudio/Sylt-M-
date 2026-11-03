#pragma once
#include <string>
#include <vector>
#include "common.hpp"
#include "diag.hpp"
namespace sylt {
enum class TT {
    Eof, Newline, Indent, Dedent,
    Ident, Int, Float, Str,
    KwFn, KwLet, KwVar, KwType, KwIf, KwElif, KwElse, KwWhile, KwFor, KwIn,
    KwReturn, KwBreak, KwContinue, KwPass, KwAnd, KwOr, KwNot, KwTrue, KwFalse,
    KwSelf, KwMut, KwMatch, KwCase, KwImport, KwForeign, KwEnum,
    Plus, Minus, Star, Slash, Percent,
    Eq, EqEq, NotEq, Lt, LtEq, Gt, GtEq,
    PlusEq, MinusEq, StarEq, SlashEq,
    Arrow, DotDot, Dot, Comma, Colon, LParen, RParen, LBracket, RBracket,
    LBrace, RBrace, Amp,
};
struct Token {
    TT T = TT::Eof;
    std::string Text;
    Span Sp;
    int64_t IntVal = 0;
    double FloatVal = 0;
};
std::vector<Token> Lex(const SourceFile& src, Reporter& rep);
std::vector<Token> LexSubstring(const std::string& text, int line, int col, Reporter& rep);
const char* TokName(TT t);
}
