#!/usr/bin/env bash
# Isaac Gamba Ureña - 2022437592. Evidencias repetibles del caso A.
set -euo pipefail
make
results_dir=$(mktemp -d pruebas/resultados_ayudantes_XXXXXX)

run_scenario() {
    local name="$1" ease="$2" limit="$3" expected="$4" delay="${5:-0.5}"
    local run seed log code
    for run in 1 2 3; do
        seed=$((41 + run))
        log="$results_dir/${name}_${run}.txt"
        local command=(./build/box --task "$name" --ease "$ease" --delay "$delay"
                       --seed "$seed" --limit "$limit" --csv "$results_dir/resumen.csv")
        printf 'Comando: ' > "$log"
        printf '%q ' "${command[@]}" >> "$log"
        printf '\n\n' >> "$log"
        set +e
        "${command[@]}" 2>&1 | tee -a "$log"
        code=${PIPESTATUS[0]}
        set -e
        if [[ "$code" != "$expected" ]]; then
            printf 'FALLO en %s: retorno %s, esperado %s.\n' "$log" "$code" "$expected"
            exit 1
        fi
    done
}

run_scenario facil 100 96 0
run_scenario intermedia 60 96 0
run_scenario dificil 20 96 0
run_scenario imposible 0 6 1
run_scenario facil_sin_pausa 100 96 0 0
run_scenario intermedia_sin_pausa 60 96 0 0
run_scenario dificil_sin_pausa 20 96 0 0
run_scenario imposible_sin_pausa 0 6 1 0
printf '\nEvidencias guardadas en: %s\n' "$results_dir"
