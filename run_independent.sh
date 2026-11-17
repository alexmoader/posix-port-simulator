#!/bin/bash
# Управляющая программа и участники запускаются отдельными командами.
cd -- "$(dirname -- "$0")" || exit 1
./port_sim_independent "$@" &
owner=$!
trap 'kill -INT "$owner" 2>/dev/null' INT TERM

# Список получаем после создания памяти и семафоров управляющей программой.
while ! list=$(./port_worker --list); do
    if ! kill -0 "$owner" 2>/dev/null; then
        wait "$owner"
        exit 1
    fi
    sleep 0.01
done
workers=""
while read -r role id; do
    ./port_worker "$role" "$id" &
    workers="$workers $!"
done <<< "$list"

status=0
wait "$owner" || status=$?
# После сигнала wait мог вернуться до завершения программы.
if kill -0 "$owner" 2>/dev/null; then
    status=0
    wait "$owner" || status=$?
fi
for pid in $workers; do
    wait "$pid" || status=1
done
exit "$status"
