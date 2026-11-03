#pragma once
#include <string>
#include <vector>
#include "ast.hpp"
namespace sylt {
struct GenOptions {
    bool Fast = false;
    std::vector<std::string> TestFns;
};
std::string GenCpp(ProgramP& prog, const GenOptions& opt);
}
