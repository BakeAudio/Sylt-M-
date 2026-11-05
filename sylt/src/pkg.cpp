#include "json.hpp"
#include "loader.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
namespace sylt {
namespace {

std::string Q(const std::string& s) { return "'" + s + "'"; }

void EjecutaDescartando(const std::string& cmd) {
    int Rc = std::system(cmd.c_str());
    (void)Rc;
}

bool NombreValido(const std::string& n) {
    if (n.empty() || n == "std") return false;
    if (!(isalpha((unsigned char)n[0]) || n[0] == '_')) return false;
    for (char c : n) {
        if (!(isalnum((unsigned char)c) || c == '_')) return false;
    }
    return true;
}

std::string BaseDeRuta(const std::string& p) {
    std::string s = p;
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    size_t i = s.rfind('/');
    return (i == std::string::npos) ? s : s.substr(i + 1);
}

std::string BaseGit(const std::string& url) {
    std::string s = url;
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".git") == 0) s = s.substr(0, s.size() - 4);
    return BaseDeRuta(s);
}

bool FuenteEsGit(const std::string& f) {
    return StartsWith(f, "http://") || StartsWith(f, "https://") ||
           StartsWith(f, "ssh://") || StartsWith(f, "git://") || StartsWith(f, "git@");
}

bool FuenteSegura(const std::string& f) {
    if (f.empty()) return false;
    for (char c : f) {
        if (c == '\'' || c == '\n' || c == '\r' || c == '\0') return false;
    }
    return true;
}

bool CrearDirs(const std::string& ruta) {
    std::string acum;
    size_t i = 0;
    if (!ruta.empty() && ruta[0] == '/') {
        acum = "/";
        i = 1;
    }
    while (true) {
        size_t j = ruta.find('/', i);
        if (j == std::string::npos) j = ruta.size();
        if (j > i) {
            if (!acum.empty() && acum.back() != '/') acum += "/";
            acum += ruta.substr(i, j - i);
            if (::mkdir(acum.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        if (j >= ruta.size()) break;
        i = j + 1;
    }
    return true;
}

std::string FechaDeHoy() {
    char buf[16];
    time_t t = time(nullptr);
    struct tm* tmv = localtime(&t);
    if (!tmv) return "";
    strftime(buf, sizeof(buf), "%Y-%m-%d", tmv);
    return buf;
}

void EscribirMeta(const std::string& PkgDir, const std::string& nombre,
                  const std::string& fuente, const std::string& version) {
    JsonValue m = JsonValue::MakeObj();
    m.Set("nombre", JsonValue::MakeText(nombre));
    m.Set("fuente", JsonValue::MakeText(fuente));
    m.Set("version", JsonValue::MakeText(version));
    m.Set("instalado", JsonValue::MakeText(FechaDeHoy()));
    std::ofstream F(PkgDir + "/" + nombre + ".meta", std::ios::binary);
    F << JsonDump(m) << "\n";
}

std::string LeerVersion(const std::string& PkgDir, const std::string& nombre) {
    std::string text;
    if (!ReadFile(PkgDir + "/" + nombre + ".meta", text)) return "";
    JsonValue m;
    std::string err;
    if (!JsonParse(text, m, err) || !m.IsObj()) return "";
    const JsonValue* v = m.At("version");
    return (v && v->IsText()) ? v->AsText() : "";
}

std::string CommitDeGit(const std::string& repo) {
    std::string cmd = "git -C " + Q(repo) + " rev-parse HEAD 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return "";
    char buf[64];
    std::string out;
    while (fgets(buf, sizeof(buf), p)) {
        out += buf;
        if (out.size() > 64) break;
    }
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
        out.pop_back();
    return out;
}

std::vector<std::string> SyDeRaiz(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n.size() > 3 && n.compare(n.size() - 3, 3, ".sy") == 0) out.push_back(n);
    }
    closedir(d);
    return out;
}

int PkgAdd(const std::string& PkgDir, const std::string& fuente,
           const std::string& nombreExplicito) {
    if (!FuenteSegura(fuente)) {
        std::cerr << "la fuente contiene caracteres que no puedo usar\n";
        return 2;
    }
    std::string nombre = nombreExplicito;
    bool esGit = FuenteEsGit(fuente);
    if (nombre.empty()) nombre = esGit ? BaseGit(fuente) : BaseDeRuta(fuente);
    if (!NombreValido(nombre)) {
        std::cerr << "nombre de paquete no válido: `" << nombre << "`\n";
        std::cerr << "los nombres usan letras, números y _ y empiezan por letra o _\n";
        std::cerr << "puedes elegirlo con: sylt pkg add " << fuente << " NOMBRE\n";
        return 2;
    }
    std::string destino = PkgDir + "/" + nombre;
    struct stat b;
    if (stat(destino.c_str(), &b) == 0) {
        std::cerr << "el paquete `" << nombre << "` ya está instalado\n";
        std::cerr << "quítalo antes con: sylt pkg remove " << nombre << "\n";
        return 1;
    }
    std::string staged;
    if (esGit) {
        if (std::system("git --version > /dev/null 2>&1") != 0) {
            std::cerr << "necesito git instalado para descargar paquetes\n";
            return 2;
        }
        std::string Tpl = "/tmp/sylt-pkg-XXXXXX";
        std::vector<char> Buf(Tpl.begin(), Tpl.end());
        Buf.push_back(0);
        char* R = mkdtemp(Buf.data());
        if (!R) {
            std::cerr << "no puedo crear el directorio temporal en /tmp\n";
            return 2;
        }
        staged = std::string(R) + "/repo";
        std::string cmd = "git clone --depth 1 " + fuente + " " + Q(staged) + " > /dev/null 2>&1";
        if (std::system(cmd.c_str()) != 0) {
            std::cerr << "no pude clonar " << fuente << "\n";
            std::cerr << "comprueba la URL y tu conexión: " << cmd << "\n";
            EjecutaDescartando(std::string("rm -rf ") + Q(R));
            return 1;
        }
    } else {
        if (stat(fuente.c_str(), &b) != 0 || !S_ISDIR(b.st_mode)) {
            std::cerr << "la fuente no es un directorio: `" << fuente << "`\n";
            std::cerr << "un paquete local es una carpeta con un archivo `<nombre>.sy`\n";
            return 2;
        }
        staged = fuente;
    }
    std::string version = "local";
    if (esGit) {
        std::string commit = CommitDeGit(staged);
        version = commit.empty() ? "git" : commit.substr(0, 12);
    }
    std::string entrada = staged + "/" + nombre + ".sy";
    if (!FileExists(entrada)) {
        std::vector<std::string> sy = SyDeRaiz(staged);
        if (!esGit && sy.size() == 1) {
            nombre = sy[0].substr(0, sy[0].size() - 3);
            if (stat((PkgDir + "/" + nombre).c_str(), &b) == 0) {
                std::cerr << "el paquete `" << nombre << "` ya está instalado\n";
                std::cerr << "quítalo antes con: sylt pkg remove " << nombre << "\n";
                return 1;
            }
            destino = PkgDir + "/" + nombre;
            entrada = staged + "/" + nombre + ".sy";
        } else if (esGit && sy.size() == 1) {
            nombre = sy[0].substr(0, sy[0].size() - 3);
            if (stat((PkgDir + "/" + nombre).c_str(), &b) == 0) {
                std::cerr << "el paquete `" << nombre << "` ya está instalado\n";
                std::cerr << "quítalo antes con: sylt pkg remove " << nombre << "\n";
                return 1;
            }
            destino = PkgDir + "/" + nombre;
            entrada = staged + "/" + nombre + ".sy";
        } else {
            std::cerr << "el paquete no tiene la entrada esperada: `" << nombre
                      << ".sy`\n";
            std::cerr << "busqué en: " << staged << "\n";
            std::cerr << "un paquete vive en una carpeta con su módulo principal "
                         "`<nombre>.sy` y los módulos que necesite junto a él\n";
            EjecutaDescartando(std::string("rm -rf ") + Q(staged));
            return 1;
        }
    }
    if (!CrearDirs(PkgDir)) {
        std::cerr << "no puedo crear el registro de paquetes en " << PkgDir << "\n";
        return 2;
    }
    if (esGit) {
        std::string mv = "mv " + Q(staged) + " " + Q(destino);
        if (std::system(mv.c_str()) != 0) {
            std::cerr << "no pude mover el paquete a su sitio\n";
            EjecutaDescartando(std::string("rm -rf ") + Q(staged));
            return 2;
        }
        std::string limpiar = staged.substr(0, staged.rfind('/'));
        EjecutaDescartando(std::string("rm -rf ") + Q(limpiar));
    } else {
        std::string cp = "cp -r " + Q(fuente + "/.") + " " + Q(destino);
        if (std::system(cp.c_str()) != 0) {
            std::cerr << "no pude copiar el paquete a su sitio\n";
            return 2;
        }
    }
    EscribirMeta(PkgDir, nombre, fuente, version);
    std::cout << "instalado: " << nombre << " (" << version << ") desde " << fuente
              << "\n";
    return 0;
}

int PkgList(const std::string& PkgDir) {
    DIR* d = opendir(PkgDir.c_str());
    if (!d) {
        std::cout << "no hay paquetes instalados\n";
        return 0;
    }
    std::vector<std::string> nombres;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n.size() > 3 && n.compare(n.size() - 3, 3, ".sy") == 0) continue;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".meta") == 0) continue;
        if (n == "." || n == "..") continue;
        struct stat b;
        std::string completo = PkgDir + "/" + n;
        if (stat(completo.c_str(), &b) != 0 || !S_ISDIR(b.st_mode)) continue;
        if (!FileExists(completo + "/" + n + ".sy")) continue;
        nombres.push_back(n);
    }
    closedir(d);
    if (nombres.empty()) {
        std::cout << "no hay paquetes instalados\n";
        return 0;
    }
    std::sort(nombres.begin(), nombres.end());
    std::cout << "registro local: " << PkgDir << "\n";
    for (auto& n : nombres) {
        std::string version = LeerVersion(PkgDir, n);
        if (version.empty()) version = "?";
        std::cout << "  " << n << "  (" << version << ")\n";
    }
    return 0;
}

int PkgRemove(const std::string& PkgDir, const std::string& nombre) {
    if (!NombreValido(nombre)) {
        std::cerr << "nombre de paquete no válido: `" << nombre << "`\n";
        return 2;
    }
    std::string destino = PkgDir + "/" + nombre;
    struct stat b;
    if (stat(destino.c_str(), &b) != 0 || !S_ISDIR(b.st_mode)) {
        std::cerr << "el paquete `" << nombre << "` no está instalado\n";
        std::cerr << "mira lo que hay con: sylt pkg list\n";
        return 1;
    }
    if (std::system((std::string("rm -rf ") + Q(destino)).c_str()) != 0) {
        std::cerr << "no pude eliminar la carpeta del paquete: " << destino << "\n";
        return 2;
    }
    std::string meta = PkgDir + "/" + nombre + ".meta";
    if (FileExists(meta)) ::unlink(meta.c_str());
    std::cout << "eliminado: " << nombre << "\n";
    return 0;
}
}
int RunPkg(const std::vector<std::string>& Pos, const std::string& PkgDirExplicito) {
    if (Pos.empty()) {
        std::cerr << "uso: syltc pkg add FUENTE [NOMBRE] | syltc pkg list | "
                     "syltc pkg remove NOMBRE\n";
        std::cerr << "FUENTE es una carpeta local o una URL de git\n";
        return 2;
    }
    std::string PkgDir = ResolvePkgDir(PkgDirExplicito);
    if (PkgDir.empty()) {
        std::cerr << "no sé dónde está el registro de paquetes\n";
        std::cerr << "usa --pkg=RUTA o la variable SYLT_PKG\n";
        return 2;
    }
    const std::string& sub = Pos[0];
    if (sub == "list") return PkgList(PkgDir);
    if (sub == "add") {
        if (Pos.size() < 2) {
            std::cerr << "falta la fuente del paquete\n";
            std::cerr << "prueba: syltc pkg add ./mi-paquete  o  "
                         "syltc pkg add https://github.com/user/paquete\n";
            return 2;
        }
        std::string nombreExplicito = Pos.size() >= 3 ? Pos[2] : "";
        if (!nombreExplicito.empty() && !NombreValido(nombreExplicito)) {
            std::cerr << "nombre de paquete no válido: `" << nombreExplicito << "`\n";
            return 2;
        }
        return PkgAdd(PkgDir, Pos[1], nombreExplicito);
    }
    if (sub == "remove") {
        if (Pos.size() < 2) {
            std::cerr << "falta el nombre del paquete a eliminar\n";
            std::cerr << "prueba: syltc pkg remove NOMBRE\n";
            return 2;
        }
        return PkgRemove(PkgDir, Pos[1]);
    }
    std::cerr << "subcomando desconocido: `" << sub << "` (valen: add, list, remove)\n";
    return 2;
}
}
