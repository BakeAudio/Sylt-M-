#pragma once
#include <string>
#include "ast.hpp"
#include "gen.hpp"
namespace sylt {
struct Unsupported {
    Span Sp;
    std::string Msg;
};
std::string GenLlvm(ProgramP& prog, const GenOptions& opt);
}
