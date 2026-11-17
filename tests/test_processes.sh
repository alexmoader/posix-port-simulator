#!/bin/bash
# Небольшие проверки полного запуска, сигналов и очистки.
set -eu
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
cd "$tmp"

check_clean() {
    test ! -e /dev/shm/acos42_memory
    test ! -e /dev/shm/sem.acos42_mutex
    if compgen -G '/dev/shm/sem.acos42_event_*' >/dev/null; then
        echo "Остались семафоры участников" >&2
        exit 1
    fi
}

for mode in unnamed named independent; do
    for strategy in staged reserved; do
        if [ "$mode" = independent ]; then
            bash "$root/run_independent.sh" --seed=42 --unlimited --delay-us=0 \
                "--strategy=$strategy" > run.txt
        else
            "$root/port_sim_$mode" --seed=42 --unlimited --delay-us=0 \
                "--strategy=$strategy" > run.txt
        fi
        grep -Eq 'Обработано \(покинули порт\): +8$' run.txt
        check_clean
    done
    echo "Полный прогон: $mode — OK"

    if [ "$mode" = independent ]; then
        bash "$root/run_independent.sh" --seed=42 --unlimited \
            --delay-us=1000000 > signal.txt &
    else
        "$root/port_sim_$mode" --seed=42 --unlimited \
            --delay-us=1000000 > signal.txt &
    fi
    pid=$!
    for ((attempt=0; attempt<500; attempt++)); do
        if grep -q 'Начало моделирования' signal.txt; then break; fi
        sleep 0.01
    done
    grep -q 'Начало моделирования' signal.txt
    # Скрипт передает TERM владельцу как SIGINT; программе подаем SIGINT напрямую.
    if [ "$mode" = independent ]; then kill -TERM "$pid"; else kill -INT "$pid"; fi
    wait "$pid"
    grep -q 'остановлено по сигналу' signal.txt
    check_clean
    echo "Остановка сигналом: $mode — OK"
done
