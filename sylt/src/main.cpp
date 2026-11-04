#include "check.hpp"
#include "diag.hpp"
#include "fmt.hpp"
#include "gen.hpp"
#include "genllvm.hpp"
#include "loader.hpp"
#include "lsp.hpp"
#include "pkg.hpp"
#include <sys/wait.h>
#include <fstream>
#include <iostream>
#include <unordered_map>
using namespace sylt;
static void PrintUsage() {
    std::cout <<
        "syltc — compilador de Sylt (0.6)\n"
        "\n"
        "Uso:\n"
        "  syltc run   archivo.sy      compila en un temporal y ejecuta\n"
        "  syltc build archivo.sy      crea un ejecutable (-o para nombrarlo)\n"
        "  syltc check archivo.sy      solo comprueba (tipos + movimiento)\n"
        "  syltc test  archivo.sy      ejecuta las pruebas Prueba* del archivo\n"
        "  syltc emit  archivo.sy      muestra el código generado (C++ o IR LLVM)\n"
        "  syltc fmt   ruta...         formatea archivos o directorios en el sitio\n"
        "  syltc lsp                   servidor de lenguaje (JSON-RPC por stdio)\n"
        "  syltc pkg   add|list|remove gestor de paquetes\n"
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
        "  --pkg=RUTA            ruta del registro local de paquetes\n"
        "                        (también $SYLT_PKG, por defecto ~/.local/share/sylt/pkg)\n"
        "  --check               con fmt: no escribe, solo informa lo que cambiaría\n"
        "  --keep                conserva el código generado (en /tmp)\n"
        "  --no-color            desactiva el color\n";
}
struct Args {
    std::string Cmd;
    std::vector<std::string> Archivos;
    std::string Out;
    std::string Cc = "g++";
    std::string Opt = "-O2";
    std::string Prelude;
    std::string Lib;
    std::string Pkg;
    std::string Backend = "cpp";
    bool Fast = false;
    bool Keep = false;
    bool Color = true;
    bool CheckFmt = false;
};
int main(int argc, char** argv) {
    Args A;
    if (argc < 2) {
        PrintUsage();
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--version" || s == "-v") {
            std::cout << "syltc 0.6.0 — compilador de Sylt\n";
            return 0;
        }
        if (s == "--help" || s == "-h" || s == "help") {
            PrintUsage();
            return 0;
        }
        if (s == "run" || s == "build" || s == "check" || s == "emit" || s == "test" ||
            s == "fmt" || s == "lsp" || s == "pkg") {
            if (!A.Cmd.empty()) {
                std::cerr << "solo un comando por invocación (" << A.Cmd << " y " << s
                          << ")\n";
                return 2;
            }
            A.Cmd = s;
            continue;
        }
        if (s == "--fast") A.Fast = true;
        else if (s == "--keep") A.Keep = true;
        else if (s == "--no-color") A.Color = false;
        else if (s == "--check") A.CheckFmt = true;
        else if (s == "--O0") A.Opt = "-O0";
        else if (s == "--O1") A.Opt = "-O1";
        else if (s == "--O2") A.Opt = "-O2";
        else if (s == "--O3") A.Opt = "-O3";
        else if (StartsWith(s, "--cc=")) A.Cc = s.substr(5);
        else if (StartsWith(s, "--backend=")) A.Backend = s.substr(10);
        else if (StartsWith(s, "--prelude=")) A.Prelude = s.substr(10);
        else if (StartsWith(s, "--lib=")) A.Lib = s.substr(6);
        else if (StartsWith(s, "--pkg=")) A.Pkg = s.substr(6);
        else if (s == "-o" && i + 1 < argc) A.Out = argv[++i];
        else if (!s.empty() && s[0] == '-') {
            std::cerr << "opción desconocida: " << s << "\n";
            return 2;
        } else {
            A.Archivos.push_back(s);
        }
    }
    if (A.Cmd.empty()) {
        if (!A.Archivos.empty())
            std::cerr << "falta el comando: prueba `syltc run " << A.Archivos[0] << "`\n";
        else
            std::cerr << "falta el comando\nprueba: syltc run archivo.sy   (o syltc --help)\n";
        return 2;
    }
    if (getenv("NO_COLOR")) A.Color = false;
    if (!isatty(1)) A.Color = false;
    if (A.Cmd == "lsp") {
        if (!A.Archivos.empty()) {
            std::cerr << "lsp no toma archivos: lee y escribe en stdin/stdout\n";
            return 2;
        }
        return RunLsp();
    }
    if (A.Cmd == "pkg") return RunPkg(A.Archivos, A.Pkg);
    if (A.Cmd == "fmt") return RunFmt(A.Archivos, A.CheckFmt);
    if (A.Archivos.empty()) {
        std::cerr << "falta el archivo .sy a compilar\n";
        std::cerr << "prueba: syltc run " << "ejemplo.sy\n";
        return 2;
    }
    if (A.Archivos.size() > 1) {
        std::cerr << "solo puedo compilar un archivo cada vez (" << A.Archivos[0]
                  << " y " << A.Archivos[1] << ")\n";
        return 2;
    }
    std::string Archivo = A.Archivos[0];
    if (A.Backend != "cpp" && A.Backend != "llvm") {
        std::cerr << "backend desconocido: `" << A.Backend << "` (valen: cpp, llvm)\n";
        return 2;
    }
    std::string Lib = ResolveLibDir(A.Lib);
    std::string Text;
    if (!ReadFile(A.Archivos[0], Text)) {
        std::cerr << "no puedo leer `" << A.Archivos[0] << "`: ¿existe y tiene permisos?\n";
        return 2;
    }
    SourceFile Src;
    Src.Load(A.Archivos[0], Text);
    Reporter Rep(Src, A.Color);
    Loader Ld(Rep, Lib);
    Ld.PkgDir = ResolvePkgDir(A.Pkg);
    if (!Ld.Load(A.Archivos[0], "__main__", false)) {
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
    Check(Prog, Src, Rep, &Files, A.Cmd == "run" || A.Cmd == "build" || A.Cmd == "emit");
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
    if (A.Cmd == "test") {
        std::vector<std::string> Tests;
        std::vector<std::string> Malas;
        for (auto& fd : Prog->Fns) {
            if (!StartsWith(fd->Name, "Prueba")) continue;
            if (fd->Module.empty() && !fd->IsMethod && !fd->Foreign && fd->Params.empty())
                Tests.push_back(fd->Name);
            else
                Malas.push_back(fd->Name);
        }
        if (Tests.empty()) {
            Diag d = Rep.ErrNoSpan("E0210", "no encontré ninguna prueba que ejecutar");
            d.Helps.push_back("`syltc test` busca funciones `fn PruebaNombre()` sin "
                              "parámetros en el archivo principal");
            d.Helps.push_back("ejemplo: fn PruebaSuma(): Afirma(2 + 2 == 4, \"la suma falla\")");
            Rep.Emit(d);
            std::cout << "no se pudo compilar: " << Rep.ErrorCount() << " error\n";
            return 1;
        }
        for (auto& n : Malas) {
            for (auto& fd : Prog->Fns) {
                if (fd->Name != n) continue;
                Diag d = Rep.Err("E0211", fd->Sp,
                    "la prueba `" + n + "` no se puede ejecutar");
                if (fd->IsMethod)
                    d.Helps.push_back("las pruebas son funciones globales, no métodos");
                else if (!fd->Params.empty())
                    d.Helps.push_back("las pruebas no reciben parámetros: `fn " + n + "()`");
                else
                    d.Helps.push_back("las pruebas viven en el archivo principal, no en un módulo");
                Rep.Emit(d);
            }
        }
        if (!Malas.empty()) {
            std::cout << "no se pudo compilar: " << Rep.ErrorCount()
                      << (Rep.ErrorCount() == 1 ? " error\n" : " errores\n");
            return 1;
        }
        Go.TestFns = Tests;
    }
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
                "el backend llvm cubre el núcleo, colecciones, std.* y pruebas; "
                "lo que falte se apunta como trabajo pendiente del roadmap");
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
    std::string Prelude = ResolvePrelude(A.Prelude);
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
    if (A.Cmd == "run" || A.Cmd == "test" || A.Keep) {
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
        std::string Base = A.Archivos[0];
        size_t S = Base.rfind('/');
        if (S != std::string::npos) Base = Base.substr(S + 1);
        size_t D = Base.rfind('.');
        if (D != std::string::npos) Base = Base.substr(0, D);
        Out = Base;
    }
    std::string RtPath;
    if (A.Backend == "llvm") {
        std::vector<std::string> Candidatos;
        if (!Lib.empty()) Candidatos.push_back(Lib + "/rt/sylt_rt.c");
        Candidatos.push_back(ExeDir() + "/../lib/rt/sylt_rt.c");
        Candidatos.push_back(ExeDir() + "/lib/rt/sylt_rt.c");
        std::string Home = HomeDir();
        if (!Home.empty())
            Candidatos.push_back(Home + "/.local/share/sylt/lib/rt/sylt_rt.c");
        for (auto& C : Candidatos)
            if (FileExists(C)) {
                RtPath = C;
                break;
            }
        if (RtPath.empty()) {
            std::cerr << "no encuentro lib/rt/sylt_rt.c (el runtime en C del backend llvm)\n";
            std::cerr << "usa --lib=RUTA para indicar dónde está la librería\n";
            return 2;
        }
    }
    std::string Cmd;
    if (A.Backend == "llvm") {
        if (A.Cc == "g++") {
            std::cerr << "el backend llvm emite IR (.ll) y lo enlaza clang: pasa --cc=clang\n";
            std::cerr << "para ver el IR sin enlazar: syltc emit --backend=llvm " << A.Archivos[0]
                      << "\n";
            return 2;
        }
        Cmd = A.Cc + " " + A.Opt + " -w \"" + CppPath + "\" \"" + RtPath + "\" -o \"" +
              (A.Cmd == "run" || A.Cmd == "test" ? TmpDir + "/prog" : Out) + "\" -lm -lpthread";
    } else {
        Cmd = A.Cc + " -std=c++17 " + A.Opt + " -w -I\"" + PreludeDir +
              "\" \"" + CppPath + "\" -o \"" +
              (A.Cmd == "run" || A.Cmd == "test" ? TmpDir + "/prog" : Out) + "\"";
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
        std::cout << "compilado: " << A.Archivos[0] << " -> " << Out << "\n";
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
