#!/bin/bash
# Corre ./mdbirp sobre todas las instancias de un directorio y guarda un CSV.
#
# Uso:   scripts/benchmark.sh <directorio_instancias> <salida.csv> [semillas...]
# Ej.:   scripts/benchmark.sh instances    bench_out/old.csv 101 102 103
#        scripts/benchmark.sh instances_v3 bench_out/v3.csv  101 102 103
#
# Variables opcionales:  JOBS=6 (procesos en paralelo; menos = tiempos mas fiables)
#                        EXTRA="--iterations 50"  (flags extra para mdbirp)
# Cada fila:  rc,instancia,semilla,costo_greedy,costo_final,equipos,iteraciones,
#             segundos,valida,equipos_greedy,viaje,despacho,alojamiento,noches,cota_equipos
# Si una corrida falla (instancia infactible o archivo inconsistente): rc,instancia,semilla,FAIL
set -u
cd "$(dirname "$0")/.."
DIR=${1:?uso: benchmark.sh <directorio> <salida.csv> [semillas...]}
OUT=${2:?uso: benchmark.sh <directorio> <salida.csv> [semillas...]}
shift 2
SEEDS=${*:-101 102 103}
JOBS=${JOBS:-6}
EXTRA=${EXTRA:-}
[ -x ./mdbirp ] || make || exit 1
mkdir -p "$(dirname "$OUT")"
TMP=$(mktemp -d)

run_one() {
  f=$1; s=$2
  line=$(timeout 900 ./mdbirp "$f" --seed "$s" --csv $EXTRA 2>/dev/null); rc=$?
  if [ $rc -eq 0 ] && [ -n "$line" ]; then echo "$rc,$line"; else echo "$rc,$(basename "$f"),$s,FAIL"; fi
}
export -f run_one
export EXTRA

for s in $SEEDS; do for f in "$DIR"/*.dat; do echo "$f $s"; done; done \
  | xargs -P "$JOBS" -L1 bash -c 'run_one $0 $1' > "$TMP/rows.csv"
sort "$TMP/rows.csv" > "$OUT"
rm -rf "$TMP"
echo "Listo: $(wc -l < "$OUT") corridas en $OUT ($(grep -c FAIL "$OUT") fallidas)"
