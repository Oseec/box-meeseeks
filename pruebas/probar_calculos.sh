#!/usr/bin/env bash
# Isaac Gamba Ureña - 2022437592. Integración B: un hijo, pipes y resumen.
set -euo pipefail
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
passed=0

check() {
    local expected="$1" pattern="$2" expression="$3"
    local code=0
    ./build/box --case B --task "$expression" > "$temporary/last.txt" 2>&1 || code=$?
    [[ "$code" == "$expected" ]]
    grep -Eq "$pattern" "$temporary/last.txt"
    grep -q 'Meeseeks creados=1 ' "$temporary/last.txt"
    grep -q 'Meeseeks activos al cierre=0' "$temporary/last.txt"
    grep -q 'cierre forzado=no' "$temporary/last.txt"
    grep -q 'Suma de pausas individuales=0.000000 s' "$temporary/last.txt"
    [[ $(grep -c "Hi, I'm Mr. Meeseeks" "$temporary/last.txt") == 1 ]]
    [[ $(grep -c 'Gracias por dejarme servirle' "$temporary/last.txt") == 1 ]]
    if grep -q 'AYUDA:' "$temporary/last.txt"; then exit 1; fi
    passed=$((passed + 1))
}

check 0 'Resultado = 14' '2+3*4'
check 0 'Resultado = 20' '(2+3)*4'
check 0 'Resultado = 1' '(8>3)&&!(2==5)'
check 0 'Resultado = 0' '3<2'
check 0 'Cortocircuito' '0 && (1/0)'
check 1 'Division entre cero' '10/0'
check 1 'Falta parentesis' '(2+3'
check 1 'Resultado fuera de rango' '(1e308*1e308)>0'
check 1 'Se esperaba' '1 || (2+)'

code=0
./build/box < pruebas/entrada_sesion_B.txt > "$temporary/menu.txt" 2>&1 || code=$?
[[ "$code" == 1 ]]
grep -q 'Solicitudes=4 | resueltas=3 | no resueltas=1' "$temporary/menu.txt"
if grep -q 'Facilidad de la tarea' "$temporary/menu.txt"; then exit 1; fi
passed=$((passed + 1))

./build/box --case B --task '0' --csv "$temporary/nuevo.csv" > /dev/null
head -n 1 "$temporary/nuevo.csv" | grep -q '^case,task,'
grep -q '^B,"0",,,' "$temporary/nuevo.csv"
passed=$((passed + 1))

printf 'task,ease,delay\n' > "$temporary/viejo.csv"
code=0
./build/box --case B --task '1+2' --csv "$temporary/viejo.csv" > /dev/null 2>&1 || code=$?
[[ "$code" == 2 && $(wc -l < "$temporary/viejo.csv") == 1 ]]
passed=$((passed + 1))

printf 'PASS: %s comprobaciones de integracion del caso B.\n' "$passed"
