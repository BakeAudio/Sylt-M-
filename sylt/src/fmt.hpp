#pragma once
#include <string>
#include <vector>
#include "diag.hpp"
namespace sylt {
bool FormatSource(const std::string& name, const std::string& text, std::string& out,
                  std::vector<Diag>* sink);
int RunFmt(std::vector<std::string> Rutas, bool SoloComprobar);
}
