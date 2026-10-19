#!/usr/bin/env bash
# Isaac Gamba Ureña - 2022437592. Tres corridas de cada escenario B.
set -euo pipefail
make
results_dir=$(mktemp -d pruebas/resultados_B_XXXXXX)

run_scenario() {
    local name="$1" expression="$2" expected="$3"
    local run log code
    for run in 1 2 3; do
        log="$results_dir/${name}_${run}.txt"
        local command=(./build/box --case B --task "$expression" --seed "$((41 + run))"
                       --csv "$results_dir/resumen_B.csv")
        printf 'Comando: ' > "$log"
        printf '%q ' "${command[@]}" >> "$log"
        printf '\n\n' >> "$log"
        set +e
        "${command[@]}" 2>&1 | tee -a "$log"
        code=${PIPESTATUS[0]}
        set -e
        if [[ "$code" != "$expected" ]]; then
            printf 'FALLO en %s: retorno %s; esperado %s.\n' "$log" "$code" "$expected"
            exit 1
        fi
    done
}

run_scenario aritmetica '2+3*4' 0
run_scenario logica '(8>3)&&!(2==5)' 0
run_scenario division_cero '10/0' 1
run_scenario parentesis '(2+3' 1
run_scenario cortocircuito '0 && (1/0)' 0
printf '\nEvidencias B guardadas en: %s\n' "$results_dir"
