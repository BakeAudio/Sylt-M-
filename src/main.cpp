#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include "check.hpp"
#include "diag.hpp"
#include "gen.hpp"
#include "genllvm.hpp"
#include "lex.hpp"
#include "parse.hpp"
using namespace sylt;
static bool ReadFile(const std::string& Path, std::string& Out) {
    std::ifstream F(Path, std::ios::binary);
    if (!F) return false;
    std::ostringstream S;
    S << F.rdbuf();
    Out = S.str();
    return true;
}
static std::string ExeDir() {
    char Buf[4096];
    ssize_t N = readlink("/proc/self/exe", Buf, sizeof(Buf) - 1);
    if (N <= 0) return ".";
    Buf[N] = 0;
    std::string P(Buf);
    size_t S = P.rfind('/');
    if (S == std::string::npos) return ".";
    return P.substr(0, S);
}
static bool FileExists(const std::string& P) {
    struct stat B;
    return stat(P.c_str(), &B) == 0;
}
static void PrintUsage() {
    std::cout <<
        "syltc — compilador de Sylt (0.3)\n"
        "\n"
        "Uso:\n"
        "  syltc run   archivo.sy      compila en un temporal y ejecuta\n"
        "  syltc build archivo.sy      crea un ejecutable (-o para nombrarlo)\n"
        "  syltc check archivo.sy      solo comprueba (tipos + movimiento)\n"
        "  syltc emit  archivo.sy      muestra el código generado (C++ o IR LLVM)\n"
        "\n"
        "Opciones:\n"
        "  --backend=cpp|llvm    cpp: transpila a C++17 (por defecto)\n"
        "                        llvm: emite IR textual (.ll), verificado por LLVM\n"
        "  --O0 --O1 --O2 --O3   nivel de optimización (por defecto -O2)\n"
        "  --fast                sin comprobaciones de límites en caliente\n"
        "  --cc=COMANDO          compilador C++ a usar (g++, clang++...)\n"
        "  --prelude=RUTA        ruta de sylt_prelude.hpp\n"
        "                        (también busca en $SYLT_PRELUDE y ~/.local/share/sylt/)\n"
        "  --lib=RUTA            ruta de la librería estándar (std.*)\n"
        "  --keep                conserva el código generado (en /tmp)\n"
        "  --no-color            desactiva el color\n";
}
struct Args {
    std::string Cmd;
    std::string File;
    std::string Out;
    std::string Cc = "g++";
    std::string Opt = "-O2";
    std::string Prelude;
    std::string Lib;
    std::string Backend = "cpp";
    bool Fast = false;
    bool Keep = false;
    bool Color = true;
};
struct ModuleUnit {
    std::string Name;
    std::string Path;
    SourceFile Src;
    ProgramP Prog;
};
struct Loader {
    Reporter& Rep;
    std::string LibDir;
    std::unordered_map<std::string, size_t> Index;
    std::vector<ModuleUnit> Units;
    std::vector<std::string> Stack;
    Loader(Reporter& rep, const std::string& lib) : Rep(rep), LibDir(lib) {}
    std::string DirOf(const std::string& path) const {
        size_t S = path.rfind('/');
        return (S == std::string::npos) ? "." : path.substr(0, S);
    }
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
        std::string Text;
        if (!ReadFile(path, Text)) {
            Diag d = Rep.ErrNoSpan("E0701",
                "no encuentro el módulo `" + name + "`");
            d.Helps.push_back("busqué en: " + path);
            if (!isStd)
                d.Helps.push_back("los módulos propios viven en un archivo `nombre.sy` "
                                  "junto al que importa");
            else
                d.Helps.push_back("si instalaste Sylt en otra ruta, usa --lib=RUTA");
            Rep.Emit(d);
            return false;
        }
        Stack.push_back(name);
        ModuleUnit U;
        U.Name = name;
        U.Path = path;
        U.Src.Load(path, Text);
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
            std::string p2 = DirOf(path) + "/" + im.Module + ".sy";
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
int main(int argc, char** argv) {
    Args A;
    if (argc < 2) {
        PrintUsage();
        return 0;
    }
    A.Cmd = argv[1];
    if (A.Cmd == "--version" || A.Cmd == "-v") {
        std::cout << "syltc 0.3.0 — compilador de Sylt\n";
        return 0;
    }
    if (A.Cmd == "--help" || A.Cmd == "-h" || A.Cmd == "help") {
        PrintUsage();
        return 0;
    }
    if (A.Cmd != "run" && A.Cmd != "build" && A.Cmd != "check" && A.Cmd != "emit") {
        std::cerr << "no sé qué hacer con `" << A.Cmd << "`\n";
        std::cerr << "prueba: syltc run archivo.sy   (o syltc --help)\n";
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--fast") A.Fast = true;
        else if (s == "--keep") A.Keep = true;
        else if (s == "--no-color") A.Color = false;
        else if (s == "--O0") A.Opt = "-O0";
        else if (s == "--O1") A.Opt = "-O1";
        else if (s == "--O2") A.Opt = "-O2";
        else if (s == "--O3") A.Opt = "-O3";
        else if (StartsWith(s, "--cc=")) A.Cc = s.substr(5);
        else if (StartsWith(s, "--backend=")) A.Backend = s.substr(10);
        else if (StartsWith(s, "--prelude=")) A.Prelude = s.substr(10);
        else if (StartsWith(s, "--lib=")) A.Lib = s.substr(6);
        else if (s == "-o" && i + 1 < argc) A.Out = argv[++i];
        else if (!s.empty() && s[0] == '-') {
            std::cerr << "opción desconocida: " << s << "\n";
            return 2;
        } else {
            if (!A.File.empty()) {
                std::cerr << "solo puedo compilar un archivo cada vez (" << A.File
                          << " y " << s << ")\n";
                return 2;
            }
            A.File = s;
        }
    }
    if (A.File.empty()) {
        std::cerr << "falta el archivo .sy a compilar\n";
        std::cerr << "prueba: syltc run " << "ejemplo.sy\n";
        return 2;
    }
    if (A.Backend != "cpp" && A.Backend != "llvm") {
        std::cerr << "backend desconocido: `" << A.Backend << "` (valen: cpp, llvm)\n";
        return 2;
    }
    if (getenv("NO_COLOR")) A.Color = false;
    if (!isatty(1)) A.Color = false;
    std::string Lib = A.Lib;
    if (Lib.empty() && getenv("SYLT_LIB")) Lib = getenv("SYLT_LIB");
    if (Lib.empty()) {
        std::string Home = getenv("HOME") ? getenv("HOME") : "";
        std::string C1 = ExeDir() + "/lib";
        std::string C2 = ExeDir() + "/../lib";
        std::string C3 = Home.empty() ? "" : Home + "/.local/share/sylt/lib";
        if (FileExists(C1)) Lib = C1;
        else if (FileExists(C2)) Lib = C2;
        else if (!C3.empty() && FileExists(C3)) Lib = C3;
    }
    std::string Text;
    if (!ReadFile(A.File, Text)) {
        std::cerr << "no puedo leer `" << A.File << "`: ¿existe y tiene permisos?\n";
        return 2;
    }
    SourceFile Src;
    Src.Load(A.File, Text);
    Reporter Rep(Src, A.Color);
    Loader Ld(Rep, Lib);
    if (!Ld.Load(A.File, "__main__", false)) {
        std::cout << "no se pudo compilar: " << Rep.ErrorCount()
                  << (Rep.ErrorCount() == 1 ? " error, " : " errores, ")
                  << Rep.WarningCount()
                  << (Rep.WarningCount() == 1 ? " aviso" : " avisos") << "\n";
        return 1;
    }
    ProgramP Prog = Ld.Merge();
    if (Rep.ErrorCount() > 0) {
        std::cout << "no se pudo compilar: " << Rep.ErrorCount()
                  << (Rep.ErrorCount() == 1 ? " error, " : " errores, ")
                  << Rep.WarningCount()
                  << (Rep.WarningCount() == 1 ? " aviso" : " avisos") << "\n";
        return 1;
    }
    Rep.Use(&Src);
    std::unordered_map<std::string, const SourceFile*> Files;
    for (auto& U : Ld.Units)
        Files[U.Name == "__main__" ? "" : U.Name] = &U.Src;
    Check(Prog, Src, Rep, &Files, A.Cmd != "check");
    if (Rep.ErrorCount() > 0) {
        std::cout << "no se pudo compilar: " << Rep.ErrorCount()
                  << (Rep.ErrorCount() == 1 ? " error, " : " errores, ")
                  << Rep.WarningCount()
                  << (Rep.WarningCount() == 1 ? " aviso" : " avisos") << "\n";
        return 1;
    }
    if (A.Cmd == "check") {
        bool hasMain = false;
        for (auto& fd : Prog->Fns)
            if (fd->Name == "Main") hasMain = true;
        std::string Avisos = Rep.WarningCount() > 0
            ? ", " + std::to_string(Rep.WarningCount()) +
              (Rep.WarningCount() == 1 ? " aviso" : " avisos") : "";
        if (hasMain)
            std::cout << "listo: tipos y movimiento correctos" << Avisos << "\n";
        else
            std::cout << "listo: módulo correcto (sin fn Main: tratado como módulo)"
                      << Avisos << "\n";
        return 0;
    }
    GenOptions Go;
    Go.Fast = A.Fast;
    std::string Code;
    if (A.Backend == "llvm") {
        try {
            Code = GenLlvm(Prog, Go);
        } catch (const Unsupported& U) {
            Rep.Use(&Src);
            Span sp = U.Sp;
            if (sp.Line < 1 || sp.Line > (int)Src.Lines.size()) sp = Span();
            Diag d = Rep.Err("E0901", sp, U.Msg);
            d.Helps.push_back(
                "el backend llvm cubre el núcleo (números, strings, structs, enums, "
                "control de flujo); las colecciones y std.* llegan en v0.4");
            d.Helps.push_back("usa --backend=cpp para compilar este programa completo");
            Rep.Emit(d);
            std::cout << "no se pudo compilar: " << Rep.ErrorCount()
                      << (Rep.ErrorCount() == 1 ? " error, " : " errores, ")
                      << Rep.WarningCount()
                      << (Rep.WarningCount() == 1 ? " aviso" : " avisos") << "\n";
            return 1;
        }
    } else {
        Code = GenCpp(Prog, Go);
    }
    if (A.Cmd == "emit") {
        std::cout << Code;
        return 0;
    }
    std::string Prelude = A.Prelude;
    if (Prelude.empty() && getenv("SYLT_PRELUDE")) Prelude = getenv("SYLT_PRELUDE");
    if (Prelude.empty()) {
        std::string Home = getenv("HOME") ? getenv("HOME") : "";
        std::string C1 = ExeDir() + "/sylt_prelude.hpp";
        std::string C2 = Home.empty() ? "" : Home + "/.local/share/sylt/sylt_prelude.hpp";
        if (FileExists(C1)) Prelude = C1;
        else if (!C2.empty() && FileExists(C2)) Prelude = C2;
        else Prelude = C1;
    }
    if (!FileExists(Prelude)) {
        std::cerr << "no encuentro sylt_prelude.hpp\n";
        std::cerr << "busqué junto al binario, en $SYLT_PRELUDE y en ~/.local/share/sylt/\n";
        std::cerr << "usa --prelude=RUTA para indicar dónde está\n";
        return 2;
    }
    std::string PreludeDir;
    {
        size_t S = Prelude.rfind('/');
        PreludeDir = (S == std::string::npos) ? "." : Prelude.substr(0, S);
    }
    std::string TmpDir;
    if (A.Cmd == "run" || A.Keep) {
        std::string Tpl = "/tmp/sylt-XXXXXX";
        std::vector<char> Buf(Tpl.begin(), Tpl.end());
        Buf.push_back(0);
        char* R = mkdtemp(Buf.data());
        if (!R) {
            std::cerr << "no puedo crear el directorio temporal en /tmp\n";
            return 2;
        }
        TmpDir = R;
    } else {
        TmpDir = "/tmp";
    }
    std::string CppPath = TmpDir + (A.Backend == "llvm" ? "/main_sylt.ll" : "/main_sylt.cpp");
    {
        std::ofstream F(CppPath, std::ios::binary);
        F << Code;
    }
    std::string Out = A.Out;
    if (Out.empty()) {
        std::string Base = A.File;
        size_t S = Base.rfind('/');
        if (S != std::string::npos) Base = Base.substr(S + 1);
        size_t D = Base.rfind('.');
        if (D != std::string::npos) Base = Base.substr(0, D);
        Out = Base;
    }
    std::string Cmd;
    if (A.Backend == "llvm") {
        if (A.Cc == "g++") {
            std::cerr << "el backend llvm emite IR (.ll) y lo enlaza clang: pasa --cc=clang\n";
            std::cerr << "para ver el IR sin enlazar: syltc emit --backend=llvm " << A.File
                      << "\n";
            return 2;
        }
        Cmd = A.Cc + " " + A.Opt + " -w \"" + CppPath + "\" -o \"" +
              (A.Cmd == "run" ? TmpDir + "/prog" : Out) + "\" -lm";
    } else {
        Cmd = A.Cc + " -std=c++17 " + A.Opt + " -w -I\"" + PreludeDir +
              "\" \"" + CppPath + "\" -o \"" +
              (A.Cmd == "run" ? TmpDir + "/prog" : Out) + "\"";
    }
    if (getenv("SYLT_VERBOSE")) std::cerr << "[syltc] " << Cmd << "\n";
    int Rc = std::system(Cmd.c_str());
    if (Rc != 0) {
        std::cerr << "el compilador de C++ (" << A.Cc
                  << ") devolvió un error inesperado.\n";
        std::cerr << "Esto casi seguro es un fallo de syltc: tus errores deberían "
                     "haber salido antes, explicados.\n";
        if (!A.Keep) std::cerr << "El código generado está en: " << CppPath << "\n";
        else std::cerr << "El código generado se conserva en: " << CppPath << "\n";
        return 2;
    }
    if (A.Cmd == "build") {
        std::cout << "compilado: " << A.File << " -> " << Out << "\n";
        return 0;
    }
    std::string RunCmd = TmpDir + "/prog";
    int Status = std::system(RunCmd.c_str());
    int ExitCode = 0;
    if (WIFEXITED(Status)) ExitCode = WEXITSTATUS(Status);
    else if (WIFSIGNALED(Status)) ExitCode = 128 + WTERMSIG(Status);
    if (!A.Keep) {
        std::string Clean = "rm -rf \"" + TmpDir + "\"";
        std::system(Clean.c_str());
    }
    return ExitCode;
}
