#pragma once
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <cstdint>
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif
#include "ast.hpp"
#include "diag.hpp"
#include "lex.hpp"
#include "parse.hpp"
namespace sylt {
namespace fs = std::filesystem;
inline bool ReadFile(const std::string& Path, std::string& Out) {
    std::error_code Ec;
    if (!fs::is_regular_file(Path, Ec)) return false;
    std::ifstream F(Path, std::ios::binary);
    if (!F) return false;
    std::ostringstream S;
    S << F.rdbuf();
    Out = S.str();
    return true;
}
inline std::string CanonicalPath(const std::string& P) {
    std::error_code Ec;
    fs::path R = fs::weakly_canonical(fs::path(P), Ec);
    if (Ec) return P;
    return R.string();
}
inline std::string ExeDir() {
#if defined(_WIN32)
    char Buf[MAX_PATH];
    DWORD N = GetModuleFileNameA(nullptr, Buf, MAX_PATH);
    if (N == 0 || N >= MAX_PATH) return ".";
    std::string P(Buf, (size_t)N);
#elif defined(__APPLE__)
    char Buf[4096];
    uint32_t Tam = sizeof(Buf);
    if (_NSGetExecutablePath(Buf, &Tam) != 0) return ".";
    std::string P(Buf);
#else
    char Buf[4096];
    ssize_t N = readlink("/proc/self/exe", Buf, sizeof(Buf) - 1);
    if (N <= 0) return ".";
    Buf[N] = 0;
    std::string P(Buf);
#endif
    std::string Dir = fs::path(P).parent_path().string();
    return Dir.empty() ? std::string(".") : Dir;
}
inline bool FileExists(const std::string& P) {
    std::error_code Ec;
    return fs::exists(P, Ec);
}
inline std::string HomeDir() {
    const char* H = getenv("HOME");
    if (H && *H) return std::string(H);
    const char* W = getenv("USERPROFILE");
    return W ? std::string(W) : std::string();
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
    std::unordered_map<std::string, std::string> NameOwner;
    std::deque<ModuleUnit> Units;
    std::vector<std::string> Stack;
    Loader(Reporter& rep, const std::string& lib) : Rep(rep), LibDir(lib) {}
    ProgramP ParseUnit(ModuleUnit& U) {
        auto Tokens = Lex(U.Src, Rep);
        return Parse(Tokens, U.Src, Rep);
    }
    bool Load(const std::string& path, const std::string& name, bool isStd, Span sp) {
        for (auto& s : Stack) {
            if (s != name) continue;
            std::string cyc;
            bool started = false;
            for (auto& p : Stack) {
                if (p == name) started = true;
                if (started) cyc += (cyc.empty() ? "" : " -> ") + p;
            }
            cyc += " -> " + name;
            Diag d = sp.Line > 0
                ? Rep.Err("E0702", sp,
                    "importación circular de módulos: " + cyc)
                : Rep.ErrNoSpan("E0702",
                    "importación circular de módulos: " + cyc);
            d.Helps.push_back("los módulos no pueden importarse entre sí en círculo; "
                              "extrae lo común a un tercer módulo");
            Rep.Emit(d);
            return false;
        }
        std::string Ruta = path;
        std::string Canon = CanonicalPath(Ruta);
        if (Index.count(Canon)) return true;
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
                Canon = CanonicalPath(Alt);
                if (Index.count(Canon)) return true;
                Leido = true;
            }
        }
        if (!Leido) {
            Diag d = sp.Line > 0
                ? Rep.Err("E0701", sp,
                    "no encuentro el módulo `" + name + "`")
                : Rep.ErrNoSpan("E0701",
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
        auto owner = NameOwner.find(name);
        if (owner != NameOwner.end() && owner->second != Canon) {
            Diag d = sp.Line > 0
                ? Rep.Err("E0703", sp,
                    "el nombre de módulo `" + name + "` ya lo usa otro archivo")
                : Rep.ErrNoSpan("E0703",
                    "el nombre de módulo `" + name + "` ya lo usa otro archivo");
            d.Helps.push_back("este import resuelve a: " + Canon);
            d.Helps.push_back("y el nombre `" + name + "` ya apunta a: " + owner->second);
            d.Helps.push_back("dos fuentes distintas no pueden compartir nombre de módulo: "
                              "renombra uno de los dos archivos");
            Rep.Emit(d);
            return false;
        }
        NameOwner[name] = Canon;
        Stack.push_back(name);
        Units.emplace_back();
        ModuleUnit& U = Units.back();
        U.Name = name;
        U.Path = Ruta;
        U.Src.Load(Ruta, Text);
        Rep.Use(&U.Src);
        U.Prog = ParseUnit(U);
        Index[Canon] = Units.size() - 1;
        bool ok = true;
        for (auto& im : U.Prog->Imports) {
            if (im.Module == "std" || StartsWith(im.Module, "std.")) {
                std::string sub = im.Module.substr(4);
                std::string p2 = LibDir + "/std/" + sub + ".sy";
                if (!Load(p2, im.Module, true, im.Sp)) ok = false;
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
            if (!Load(p2, im.Module, false, im.Sp)) ok = false;
        }
        Stack.pop_back();
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
