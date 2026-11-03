#pragma once
#include <string>
#include "ast.hpp"
namespace sylt {
struct GenOptions {
    bool Fast = false;
};
std::string GenCpp(ProgramP& prog, const GenOptions& opt);
}
