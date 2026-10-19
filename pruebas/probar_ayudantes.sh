#!/usr/bin/env bash
# Isaac Gamba Ureña - 2022437592. Pruebas de integración del caso A.
# Ejecutar con make test desde la raíz del repositorio.
set -euo pipefail

temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
passed=0

check() {
    local expected="$1" pattern="$2"
    shift 2
    local code=0
    ./build/box "$@" > "$temporary/last.txt" 2>&1 || code=$?
    if [[ "$code" != "$expected" ]] || ! grep -Eq "$pattern" "$temporary/last.txt"; then
        cat "$temporary/last.txt"
        printf 'FALLO: esperado retorno %s, obtenido %s.\n' "$expected" "$code"
        exit 1
    fi
    if [[ "$expected" != 2 ]]; then
        local created greetings farewells
        created=$(sed -n 's/^Meeseeks creados=\([0-9]*\).*/\1/p' "$temporary/last.txt")
        greetings=$(grep -c "Hi, I'm Mr. Meeseeks" "$temporary/last.txt" || true)
        farewells=$(grep -c 'Gracias por dejarme servirle' "$temporary/last.txt" || true)
        [[ "$created" == "$greetings" && "$created" == "$farewells" ]]
        grep -q 'Meeseeks activos al cierre=0' "$temporary/last.txt"
        grep -q 'cierre forzado=no' "$temporary/last.txt"
    fi
    passed=$((passed + 1))
}

check 0 'Meeseeks creados=1 ' --task Facil --ease 100 --delay 0.01 --seed 42
check 0 'Meeseeks creados=1 ' --task Superior --ease 85.01 --delay 0.01 --seed 42
check 0 'minimo=2' --task Umbral --ease 85 --delay 0.02 --seed 42
check 0 'minimo=5' --task Dificil --ease 45 --delay 0.02 --seed 42
check 0 'minimo=2' --task Intermedia --ease 45.01 --delay 0.02 --seed 42
check 0 'N=4 ' --task MuyDificil --ease 0.00001 --delay 0.02 --seed 42
check 1 'LIMITE_RECURSOS' --task Imposible --ease 0 --delay 0.01 --limit 6 --seed 42
if grep -q 'SOLUCION encontrada' "$temporary/last.txt"; then exit 1; fi
check 0 'RESUMEN #1 \| RESUELTA' --task Azar --ease random --delay 0.01 --seed 42
check 0 'aleatoria' --task PausaAleatoria --ease 100 --delay random --seed 42
check 2 'invalido' --ease nan
check 2 'invalido' --ease 101
check 2 'invalido' --delay -1
check 2 'invalido' --limit 5
check 2 'invalido' --limit 6.5
check 2 'invalido' --timeout 299
check 2 'requiere' --observe
check 2 'requiere' --observe --ceiling 300

code=0
./build/box --seed 42 < pruebas/entrada_sesion_ayudantes.txt > "$temporary/menu.txt" 2>&1 || code=$?
[[ "$code" == 1 ]]
grep -q 'Solicitudes=4 | resueltas=3 | no resueltas=1' "$temporary/menu.txt"
passed=$((passed + 1))

./build/box --task 'Texto, con "comillas"' --ease 100 --delay 0 --csv "$temporary/resumen.csv" > /dev/null
grep -q '^A,"Texto, con ""comillas""",' "$temporary/resumen.csv"
passed=$((passed + 1))

printf 'PASS: %s comprobaciones de integracion; contadores, despedidas y cierre verificados.\n' "$passed"
