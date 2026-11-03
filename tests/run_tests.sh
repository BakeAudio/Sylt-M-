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
for EJ in Hola Fib Figuras Colecciones Prestamos Estados Modulos Externo; do
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

echo "== servidor HTTP en Sylt =="
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
