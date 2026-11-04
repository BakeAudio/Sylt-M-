#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYLTC = os.path.join(RAIZ, "syltc")
LL = "/tmp/sylt_llvm_caso.ll"
BIN = "/tmp/sylt_llvm_caso_bin"
RT = os.path.join(RAIZ, "lib", "rt", "sylt_rt.c")

SIN_TIEMPO = ("tiempo total", "por fotograma", "FPS")


def dir_llvm():
    candidatos = [
        os.path.expanduser("~/.local/llvm19/usr/lib/llvm-19/bin"),
        "/usr/lib/llvm-19/bin",
        "/usr/lib/llvm-18/bin",
        "/usr/lib/llvm-17/bin",
        "/usr/lib/llvm-16/bin",
    ]
    for c in candidatos:
        if os.path.exists(os.path.join(c, "llc")):
            return c
    llc = shutil.which("llc")
    if llc:
        return os.path.dirname(os.path.realpath(llc))
    return None


def clang_de(dirbin):
    for nombre in ("clang-19", "clang-18", "clang-17", "clang-16", "clang"):
        p = os.path.join(dirbin, nombre)
        if os.path.exists(p):
            return p
    return shutil.which("clang")


def correr(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def limpiar(texto, filtrar_tiempo):
    lineas = []
    for l in texto.splitlines():
        if filtrar_tiempo and any(s in l for s in SIN_TIEMPO):
            continue
        lineas.append(l)
    return "\n".join(lineas)


def main():
    dirbin = dir_llvm()
    if not dirbin:
        print("llc no está disponible: el bloque llvm de la suite se salta")
        return 0
    clang = clang_de(dirbin)
    if not clang:
        print("clang no está disponible: el bloque llvm de la suite se salta")
        return 0
    casos = [
        "tests/llvm/PruebaNucleo.sy",
        "examples/Hola.sy",
        "examples/Fib.sy",
        "examples/Figuras.sy",
        "examples/Prestamos.sy",
        "examples/Modulos.sy",
        "examples/Genericos.sy",
        "examples/Colecciones.sy",
        "examples/Estados.sy",
        "examples/Json.sy",
    ]
    con_tiempo = ["examples/BenchJuego.sy"]
    rechazados = ["examples/Externo.sy"]
    ok = 0
    fallos = 0
    for caso in casos + con_tiempo:
        filtrar = caso in con_tiempo
        ruta = os.path.join(RAIZ, caso)
        cpp = correr([SYLTC, "run", ruta])
        em = correr([SYLTC, "emit", "--backend=llvm", ruta])
        if em.returncode != 0:
            print(f"  FALLO: {caso}: no emite IR")
            print("  " + (em.stdout + em.stderr).strip().replace("\n", "\n  ")[:600])
            fallos += 1
            continue
        with open(LL, "w") as f:
            f.write(em.stdout)
        v = correr([os.path.join(dirbin, "llc"), LL, "-o", "/dev/null"])
        if v.returncode != 0:
            print(f"  FALLO: {caso}: llc rechaza el IR")
            print("  " + v.stderr.strip()[:600])
            fallos += 1
            continue
        c = correr([clang, LL, RT, "-o", BIN, "-lm", "-lpthread"])
        if c.returncode != 0:
            print(f"  FALLO: {caso}: clang no compila el IR")
            print("  " + c.stderr.strip()[:600])
            fallos += 1
            continue
        r = correr([BIN])
        a = limpiar(cpp.stdout, filtrar)
        b = limpiar(r.stdout, filtrar)
        if a != b or r.returncode != cpp.returncode:
            print(f"  FALLO: {caso}: salida distinta entre backends")
            print(f"    C++  (exit {cpp.returncode}): {cpp.stdout!r}")
            print(f"    LLVM (exit {r.returncode}): {r.stdout!r}")
            if r.stderr:
                print(f"    stderr LLVM: {r.stderr.strip()[:400]}")
            fallos += 1
            continue
        ok += 1
        print(f"  ok: {caso}")
    for caso in rechazados:
        ruta = os.path.join(RAIZ, caso)
        em = correr([SYLTC, "emit", "--backend=llvm", ruta])
        if em.returncode == 0 or "E0901" not in (em.stdout + em.stderr):
            print(f"  FALLO: {caso}: debería rechazarse con E0901 en el backend llvm")
            fallos += 1
        else:
            ok += 1
            print(f"  ok: {caso} rechazado con E0901")
    ccnombre = "clang-19" if os.path.exists(os.path.join(dirbin, "clang-19")) else "clang"
    test_llvm = [SYLTC, "test", "--backend=llvm", "--cc=" + clang, "examples/Pruebas.sy"]
    t = correr(test_llvm)
    if t.returncode == 0 and "5 pruebas, 0 fallos" in t.stdout:
        ok += 1
        print("  ok: sylt test --backend=llvm (5 pruebas, 0 fallos)")
    else:
        print("  FALLO: sylt test --backend=llvm")
        print("  " + (t.stdout + t.stderr).strip()[:600])
        fallos += 1
    print(f"backend llvm: {ok} pruebas superadas, {fallos} fallos")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
