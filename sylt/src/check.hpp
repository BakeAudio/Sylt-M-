#pragma once
#include "ast.hpp"
#include "diag.hpp"
#include <unordered_map>
namespace sylt {
bool Check(ProgramP& prog, const SourceFile& src, Reporter& rep,
           const std::unordered_map<std::string, const SourceFile*>* files = nullptr,
           bool requireMain = true);
}
