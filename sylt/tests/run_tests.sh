#!/bin/bash
set -u
cd "$(dirname "$0")/.."
SYLTC=./syltc
FALLOS=0
PASAN=0

decir_ok() {
    PASAN=$((PASAN + 1))
    echo "  ok: $1"
}

decir_fallo() {
    FALLOS=$((FALLOS + 1))
    echo "  FALLO: $1"
}

echo "== ejemplos que deben funcionar =="
for EJ in Hola Fib Figuras Colecciones Prestamos Estados Modulos Externo Genericos Json Hilos; do
    if $SYLTC run "examples/$EJ.sy" > /tmp/sylt_test_out.txt 2>&1; then
        decir_ok "$EJ.sy"
    else
        decir_fallo "$EJ.sy"
        head -15 /tmp/sylt_test_out.txt
    fi
done

echo "== ejemplos que deben fallar (con su código) =="
declare -A ESPERADO=(
    [UsoDespuesDeMover]=E0501
    [PrestamoConflictivo]=E0503
    [MutacionInmutable]=E0504
    [TipoErroneo]=E0401
    [FaltaCampo]=E0403
    [VariableTypo]=E0301
    [MatchIncompleto]=E0601
    [VarianteErronea]=E0602
    [ModuloFantasma]=E0701
    [ExternoInvalido]=E0801
    [GenericoSinTipos]=E0310
    [GenericoAridad]=E0311
    [MoverAPila]=E0501
)
for EJ in "${!ESPERADO[@]}"; do
    SALIDA=$($SYLTC check "examples/errores/$EJ.sy" 2>&1)
    if [ $? -eq 0 ]; then
        decir_fallo "$EJ.sy compiló sin error (esperaba ${ESPERADO[$EJ]})"
    elif echo "$SALIDA" | grep -q "${ESPERADO[$EJ]}"; then
        decir_ok "$EJ.sy reporta ${ESPERADO[$EJ]}"
    else
        decir_fallo "$EJ.sy reporta otro error (esperaba ${ESPERADO[$EJ]})"
        echo "$SALIDA" | head -8
    fi
done

echo "== sylt test: pruebas integradas =="
SALIDA=$($SYLTC test examples/Pruebas.sy 2>&1)
COD=$?
if [ $COD -eq 0 ] && echo "$SALIDA" | grep -q "5 pruebas, 0 fallos" && [ $(echo "$SALIDA" | grep -c '^ok ') -eq 5 ]; then
    decir_ok "Pruebas.sy: 5 pruebas, 0 fallos"
else
    decir_fallo "Pruebas.sy debería pasar 5 pruebas"
    echo "$SALIDA" | head -8
fi
SALIDA=$($SYLTC test examples/PruebasFallan.sy 2>&1)
COD=$?
if [ $COD -eq 1 ] && echo "$SALIDA" | grep -q "fallo: PruebaSumaMal" && echo "$SALIDA" | grep -q "2 pruebas, 1 fallo"; then
    decir_ok "PruebasFallan.sy: informa el fallo y sale con 1"
else
    decir_fallo "PruebasFallan.sy debería fallar con informe (exit 1)"
    echo "$SALIDA" | head -8
fi
SALIDA=$($SYLTC test examples/Hola.sy 2>&1)
if echo "$SALIDA" | grep -q "E0210"; then
    decir_ok "test sin pruebas Prueba* reporta E0210"
else
    decir_fallo "test sobre Hola.sy debería reportar E0210"
    echo "$SALIDA" | head -8
fi

echo "== módulos: check los acepta, run exige Main =="
SALIDA=$($SYLTC check examples/matematicas.sy 2>&1)
if [ $? -eq 0 ] && echo "$SALIDA" | grep -q "módulo correcto"; then
    decir_ok "check acepta matematicas.sy como módulo"
else
    decir_fallo "check debería aceptar matematicas.sy como módulo"
    echo "$SALIDA" | head -6
fi
SALIDA=$($SYLTC run examples/matematicas.sy 2>&1)
if echo "$SALIDA" | grep -q "E0302"; then
    decir_ok "run exige Main en un módulo (E0302)"
else
    decir_fallo "run debería rechazar matematicas.sy con E0302"
    echo "$SALIDA" | head -6
fi

echo "== fmt: espacios, comentarios, strings, idempotencia =="
DIR_FMT=$(mktemp -d)
cat > "$DIR_FMT/desordenado.sy" <<'EOF'
# el formateador conserva este comentario
fn   Suma( A :int,B:int )->int:
    return A+B   # y este de fin de línea
fn Main():
    let Edades = { "Ana":31, "Luis":25 }
    let X=-1+2
    if X>0:
        Print("suma: {Suma(X, 3)}")
        let Ana = Edades["Ana"]
        Print("texto con \"escapes\" y edad {Ana}")
EOF
cp "$DIR_FMT/desordenado.sy" "$DIR_FMT/otra_vez.sy"
if ./syltc fmt "$DIR_FMT/desordenado.sy" > /tmp/sylt_fmt_log.txt 2>&1; then
    FMT_OK=1
    for PATRON in 'fn Suma(A: int, B: int) -> int:' '# el formateador conserva este comentario' \
                  'return A + B # y este de fin de línea' '{ "Ana": 31, "Luis": 25 }' \
                  'let X = -1 + 2' 'if X > 0:' 'Print("suma: {Suma(X, 3)}")'; do
        grep -qF "$PATRON" "$DIR_FMT/desordenado.sy" || FMT_OK=0
    done
    grep -qF 'texto con \"escapes\" y edad {Ana}' "$DIR_FMT/desordenado.sy" || FMT_OK=0
    SALIDA_FMT=$(./syltc run "$DIR_FMT/desordenado.sy" 2>&1)
    if [ "$SALIDA_FMT" != 'suma: 4
texto con "escapes" y edad 31' ]; then
        FMT_OK=0
        echo "$SALIDA_FMT" | head -4
    fi
    ./syltc fmt "$DIR_FMT/desordenado.sy" > /dev/null 2>&1
    if ./syltc fmt --check "$DIR_FMT/desordenado.sy" > /dev/null 2>&1; then
        IDP=1
    else
        IDP=0
    fi
    if ./syltc fmt --check "$DIR_FMT/otra_vez.sy" > /dev/null 2>&1; then
        CHK=0
    else
        CHK=1
    fi
    if [ $FMT_OK -eq 1 ] && [ $IDP -eq 1 ] && [ $CHK -eq 1 ]; then
        decir_ok "fmt: estilo canónico, comentarios y strings intactos, idempotente, --check avisa"
    else
        decir_fallo "fmt: ok=$FMT_OK idempotencia=$IDP check=$CHK"
        cat "$DIR_FMT/desordenado.sy"
        head -5 /tmp/sylt_fmt_log.txt
    fi
else
    decir_fallo "fmt: no pudo formatear un archivo válido"
    cat /tmp/sylt_fmt_log.txt | head -10
fi
if ./syltc fmt --check examples lib tests > /tmp/sylt_fmt_repo.txt 2>&1; then
    decir_ok "fmt: el repositorio entero está en el estilo canónico"
else
    decir_fallo "fmt: archivos del repositorio sin el estilo canónico"
    head -8 /tmp/sylt_fmt_repo.txt
fi

echo "== pkg: add, import, list, remove =="
DIR_PKG=$(mktemp -d)
export SYLT_PKG="$DIR_PKG"
mkdir -p /tmp/sylt_paquete_saludos
cat > /tmp/sylt_paquete_saludos/saludos.sy <<'EOF'
fn Saludo() -> string:
    return "hola desde el paquete"

fn Doble(N: int) -> int:
    return N * 2
EOF
cat > "$DIR_FMT/usa_pkg.sy" <<'EOF'
import saludos

fn Main():
    Print(saludos.Saludo())
    Print("el doble de 21 es {saludos.Doble(21)}")
EOF
PKG_OK=1
if ./syltc pkg add /tmp/sylt_paquete_saludos > /tmp/sylt_pkg_log.txt 2>&1; then
    SALIDA_PKG=$(./syltc run "$DIR_FMT/usa_pkg.sy" 2>&1)
    [ "$SALIDA_PKG" = 'hola desde el paquete
el doble de 21 es 42' ] || PKG_OK=0
    ./syltc pkg list > /tmp/sylt_pkg_log.txt 2>&1
    grep -q "saludos" /tmp/sylt_pkg_log.txt || PKG_OK=0
    ./syltc pkg remove saludos > /dev/null 2>&1
    SALIDA_PKG=$(./syltc run "$DIR_FMT/usa_pkg.sy" 2>&1)
    echo "$SALIDA_PKG" | grep -q "E0701" || PKG_OK=0
    echo "$SALIDA_PKG" | grep -q "sylt pkg add" || PKG_OK=0
    if [ $PKG_OK -eq 1 ]; then
        decir_ok "pkg: add instala, el módulo se resuelve, list y remove funcionan"
    else
        decir_fallo "pkg: ciclo add/import/list/remove incompleto"
        cat /tmp/sylt_pkg_log.txt | head -6
        echo "$SALIDA_PKG" | head -4
    fi
else
    decir_fallo "pkg: add falló con una carpeta válida"
    cat /tmp/sylt_pkg_log.txt | head -6
fi
unset SYLT_PKG

echo "== lsp: initialize, diagnósticos, formato, salida limpia =="
if python3 tests/lsp_prueba.py > /tmp/sylt_lsp_log.txt 2>&1; then
    decir_ok "lsp: protocolo completo sin contaminar stdout"
    tail -1 /tmp/sylt_lsp_log.txt
else
    decir_fallo "lsp"
    cat /tmp/sylt_lsp_log.txt | head -12
fi

echo "== servidor HTTP en Sylt (con keep-alive) =="
$SYLTC build examples/ServidorHttp.sy -o /tmp/sylt_servidor_test > /dev/null 2>&1
if [ $? -ne 0 ]; then
    decir_fallo "no compila ServidorHttp.sy"
else
    /tmp/sylt_servidor_test > /tmp/sylt_servidor_log.txt 2>&1 &
    PID=$!
    sleep 1
    R1=$(curl -s http://127.0.0.1:8080/saludo)
    COD=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:8080/inexistente)
    if [ "$R1" = "¡Hola desde Sylt!" ]; then
        decir_ok "servidor responde /saludo"
    else
        decir_fallo "servidor /saludo devolvió: $R1"
    fi
    if [ "$COD" = "404" ]; then
        decir_ok "servidor responde 404 en ruta desconocida"
    else
        decir_fallo "esperaba 404, llegó $COD"
    fi
    DOBLE=$(curl -sv http://127.0.0.1:8080/saludo http://127.0.0.1:8080/saludo 2>&1)
    REUSOS=$(echo "$DOBLE" | grep -c "Re-using existing")
    HOLAS=$(echo "$DOBLE" | grep -c "¡Hola desde Sylt!")
    if [ "$REUSOS" -ge 1 ] && [ "$HOLAS" -eq 2 ]; then
        decir_ok "keep-alive: dos peticiones en la misma conexión"
    else
        decir_fallo "keep-alive: reusos=$REUSOS holas=$HOLAS"
    fi
    kill $PID 2>/dev/null
    wait $PID 2>/dev/null
fi

echo "== servidor HTTP con hilos (concurrencia real) =="
$SYLTC build examples/ServidorHilos.sy -o /tmp/sylt_servh_test > /dev/null 2>&1
if [ $? -ne 0 ]; then
    decir_fallo "no compila ServidorHilos.sy"
else
    /tmp/sylt_servh_test > /tmp/sylt_servh_log.txt 2>&1 &
    PID=$!
    sleep 1
    curl -s --max-time 5 http://127.0.0.1:8081/lento > /tmp/sylt_lento.txt 2>&1 &
    sleep 0.1
    INI=$(date +%s%N)
    R3=$(curl -s --max-time 3 http://127.0.0.1:8081/saludo)
    FIN=$(date +%s%N)
    MS=$(( (FIN - INI) / 1000000 ))
    if [ "$R3" = "¡Hola desde Sylt!" ] && [ $MS -lt 400 ]; then
        decir_ok "concurrencia: /saludo respondió en ${MS}ms con /lento en vuelo"
    else
        decir_fallo "concurrencia: saludo='$R3' tardó ${MS}ms"
    fi
    sleep 1
    LENTO=$(cat /tmp/sylt_lento.txt)
    if [ "$LENTO" = "terminé de esperar 800 ms" ]; then
        decir_ok "la petición lenta terminó intacta"
    else
        decir_fallo "la petición lenta devolvió: $LENTO"
    fi
    kill $PID 2>/dev/null
    wait $PID 2>/dev/null
fi

echo "== benchmark rápido =="
$SYLTC run examples/BenchJuego.sy > /tmp/sylt_bench.txt 2>&1
if [ $? -eq 0 ]; then
    decir_ok "BenchJuego.sy"
    tail -2 /tmp/sylt_bench.txt
else
    decir_fallo "BenchJuego.sy"
fi

echo "== backend llvm: verificación con llc + salida idéntica =="
LLC="$HOME/.local/llvm19/usr/lib/llvm-19/bin/llc"
if ! command -v llc > /dev/null 2>&1 && [ ! -x "$LLC" ]; then
    echo "  (saltado: no hay llc instalado)"
else
    if python3 tests/llvm_diff.py > /tmp/sylt_llvm_log.txt 2>&1; then
        decir_ok "backend llvm: $(grep -c '  ok:' /tmp/sylt_llvm_log.txt) casos"
        tail -1 /tmp/sylt_llvm_log.txt
    else
        decir_fallo "backend llvm"
        cat /tmp/sylt_llvm_log.txt
    fi
fi

echo ""
if [ $FALLOS -eq 0 ]; then
    echo "TODO BIEN: $PASAN tests superados"
    exit 0
else
    echo "HAY $FALLOS fallos de $((PASAN + FALLOS)) tests"
    exit 1
fi
