#!/usr/bin/env python3
import os
import subprocess
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SYLTC = os.path.join(RAIZ, "syltc")
LLVM = os.path.expanduser("~/.local/llvm19/usr/lib/llvm-19/bin")
LL = "/tmp/sylt_llvm_caso.ll"
BIN = "/tmp/sylt_llvm_caso_bin"


def correr(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def main():
    if not os.path.exists(os.path.join(LLVM, "llc")):
        print("llc no está disponible: el bloque llvm de la suite se salta")
        return 0
    casos = [
        "tests/llvm/PruebaNucleo.sy",
        "examples/Hola.sy",
        "examples/Fib.sy",
        "examples/Figuras.sy",
        "examples/Prestamos.sy",
        "examples/Modulos.sy",
    ]
    rechazados = [
        "examples/Colecciones.sy",
        "examples/BenchJuego.sy",
        "examples/Estados.sy",
        "examples/ServidorHttp.sy",
    ]
    ok = 0
    fallos = 0
    for caso in casos:
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
        v = correr([os.path.join(LLVM, "llc"), LL, "-o", "/dev/null"])
        if v.returncode != 0:
            print(f"  FALLO: {caso}: llc rechaza el IR")
            print("  " + v.stderr.strip()[:600])
            fallos += 1
            continue
        c = correr([os.path.join(LLVM, "clang-19"), LL, "-o", BIN, "-lm"])
        if c.returncode != 0:
            print(f"  FALLO: {caso}: clang no compila el IR")
            print("  " + c.stderr.strip()[:600])
            fallos += 1
            continue
        r = correr([BIN])
        if r.stdout != cpp.stdout or r.returncode != cpp.returncode:
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
    print(f"backend llvm: {ok} pruebas superadas, {fallos} fallos")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
