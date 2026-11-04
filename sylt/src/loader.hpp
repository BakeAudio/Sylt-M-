#pragma once
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include "ast.hpp"
#include "diag.hpp"
#include "lex.hpp"
#include "parse.hpp"
namespace sylt {
inline bool ReadFile(const std::string& Path, std::string& Out) {
    std::ifstream F(Path, std::ios::binary);
    if (!F) return false;
    std::ostringstream S;
    S << F.rdbuf();
    Out = S.str();
    return true;
}
inline std::string ExeDir() {
    char Buf[4096];
    ssize_t N = readlink("/proc/self/exe", Buf, sizeof(Buf) - 1);
    if (N <= 0) return ".";
    Buf[N] = 0;
    std::string P(Buf);
    size_t S = P.rfind('/');
    if (S == std::string::npos) return ".";
    return P.substr(0, S);
}
inline bool FileExists(const std::string& P) {
    struct stat B;
    return stat(P.c_str(), &B) == 0;
}
inline std::string HomeDir() {
    const char* H = getenv("HOME");
    return H ? std::string(H) : std::string();
}
inline std::string DirOfFile(const std::string& path) {
    size_t S = path.rfind('/');
    return (S == std::string::npos) ? std::string(".") : path.substr(0, S);
}
inline std::string ResolveLibDir(const std::string& Explicit) {
    if (!Explicit.empty()) return Explicit;
    const char* Env = getenv("SYLT_LIB");
    if (Env && *Env) return Env;
    std::string C1 = ExeDir() + "/lib";
    std::string C2 = ExeDir() + "/../lib";
    std::string Home = HomeDir();
    std::string C3 = Home.empty() ? "" : Home + "/.local/share/sylt/lib";
    if (FileExists(C1)) return C1;
    if (FileExists(C2)) return C2;
    if (!C3.empty() && FileExists(C3)) return C3;
    return "";
}
inline std::string ResolvePkgDir(const std::string& Explicit) {
    if (!Explicit.empty()) return Explicit;
    const char* Env = getenv("SYLT_PKG");
    if (Env && *Env) return Env;
    std::string Home = HomeDir();
    if (Home.empty()) return "";
    return Home + "/.local/share/sylt/pkg";
}
inline std::string ResolvePrelude(const std::string& Explicit) {
    if (!Explicit.empty()) return Explicit;
    const char* Env = getenv("SYLT_PRELUDE");
    if (Env && *Env) return Env;
    std::string C1 = ExeDir() + "/sylt_prelude.hpp";
    std::string Home = HomeDir();
    std::string C2 = Home.empty() ? "" : Home + "/.local/share/sylt/sylt_prelude.hpp";
    if (FileExists(C1)) return C1;
    if (!C2.empty() && FileExists(C2)) return C2;
    return C1;
}
struct ModuleUnit {
    std::string Name;
    std::string Path;
    SourceFile Src;
    ProgramP Prog;
};
struct Loader {
    Reporter& Rep;
    std::string LibDir;
    std::string PkgDir;
    std::unordered_map<std::string, std::string>* Overlays = nullptr;
    std::unordered_map<std::string, size_t> Index;
    std::vector<ModuleUnit> Units;
    std::vector<std::string> Stack;
    Loader(Reporter& rep, const std::string& lib) : Rep(rep), LibDir(lib) {}
    ProgramP ParseUnit(ModuleUnit& U) {
        auto Tokens = Lex(U.Src, Rep);
        return Parse(Tokens, U.Src, Rep);
    }
    bool Load(const std::string& path, const std::string& name, bool isStd) {
        for (auto& s : Stack) {
            if (s != name) continue;
            std::string cyc;
            bool started = false;
            for (auto& p : Stack) {
                if (p == name) started = true;
                if (started) cyc += (cyc.empty() ? "" : " -> ") + p;
            }
            cyc += " -> " + name;
            Diag d = Rep.ErrNoSpan("E0702",
                "importación circular de módulos: " + cyc);
            d.Helps.push_back("los módulos no pueden importarse entre sí en círculo; "
                              "extrae lo común a un tercer módulo");
            Rep.Emit(d);
            return false;
        }
        if (Index.count(name)) return true;
        std::string Ruta = path;
        std::string Text;
        bool Leido = false;
        if (Overlays && Overlays->count(Ruta)) {
            Text = (*Overlays)[Ruta];
            Leido = true;
        }
        if (!Leido) Leido = ReadFile(Ruta, Text);
        if (!Leido && !isStd && !PkgDir.empty()) {
            std::string Alt = PkgDir + "/" + name + "/" + name + ".sy";
            if (ReadFile(Alt, Text)) {
                Ruta = Alt;
                Leido = true;
            }
        }
        if (!Leido) {
            Diag d = Rep.ErrNoSpan("E0701",
                "no encuentro el módulo `" + name + "`");
            d.Helps.push_back("busqué en: " + Ruta);
            if (!isStd && !PkgDir.empty())
                d.Helps.push_back("y en el registro local de paquetes: " + PkgDir);
            if (!isStd) {
                d.Helps.push_back("los módulos propios viven en un archivo `nombre.sy` "
                                  "junto al que importa");
                d.Helps.push_back("si es un paquete externo, instálalo con "
                                  "`sylt pkg add <fuente>`");
            } else {
                d.Helps.push_back("si instalaste Sylt en otra ruta, usa --lib=RUTA");
            }
            Rep.Emit(d);
            return false;
        }
        Stack.push_back(name);
        ModuleUnit U;
        U.Name = name;
        U.Path = Ruta;
        U.Src.Load(Ruta, Text);
        Rep.Use(&U.Src);
        U.Prog = ParseUnit(U);
        bool ok = true;
        for (auto& im : U.Prog->Imports) {
            if (im.Module == "std" || StartsWith(im.Module, "std.")) {
                std::string sub = im.Module.substr(4);
                std::string p2 = LibDir + "/std/" + sub + ".sy";
                if (!Load(p2, im.Module, true)) ok = false;
                continue;
            }
            if (im.Module.find('.') != std::string::npos) {
                Diag d = Rep.Err("E0701", im.Sp,
                    "el módulo `" + im.Module + "` no existe");
                d.Helps.push_back("los puntos solo valen para la librería estándar (std.*); "
                                  "los módulos propios son un solo nombre");
                Rep.Emit(d);
                ok = false;
                continue;
            }
            std::string p2 = DirOfFile(Ruta) + "/" + im.Module + ".sy";
            if (!Load(p2, im.Module, false)) ok = false;
        }
        Stack.pop_back();
        Index[name] = Units.size();
        Units.push_back(std::move(U));
        Rep.Use(nullptr);
        return ok;
    }
    ProgramP Merge() {
        ProgramP all(new Program);
        for (auto& U : Units) {
            std::string mod = (U.Name == "__main__") ? "" : U.Name;
            for (auto& inc : U.Prog->ForeignIncludes) {
                bool have = false;
                for (auto& s : all->ForeignIncludes) have |= (s == inc);
                if (!have) all->ForeignIncludes.push_back(inc);
            }
            for (auto& im : U.Prog->Imports) {
                bool have = false;
                for (auto& s : all->Imports) have |= (s.Module == im.Module);
                if (!have) all->Imports.push_back(im);
            }
            for (auto& fd : U.Prog->Fns) {
                fd->Module = mod;
                all->Fns.push_back(std::move(fd));
            }
            for (auto& td : U.Prog->Types) {
                td->Module = mod;
                for (auto& m : td->Methods) m->Module = mod;
                all->Types.push_back(std::move(td));
            }
            for (auto& ed : U.Prog->Enums) {
                ed->Module = mod;
                all->Enums.push_back(std::move(ed));
            }
        }
        return all;
    }
};
}
