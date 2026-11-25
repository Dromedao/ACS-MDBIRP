# MDBIRP: Ant Colony System

Implementación en C++17 del **Multi-depot Bridge Inspection and Routing Problem (MDBIRP)**: lectura de la instancia, representación de la solución, verificación independiente de las restricciones, inicialización greedy (Ec. 10 con τ uniforme y q0 = 1), un ACS completo sobre la misma construcción y búsqueda local.

Incluye dos conjuntos de instancias:

| Carpeta | Instancias | Tamaños (puentes) | Nombre |
|---|---|---|---|
| `instances/` | 161 | 10, 15, 20, 25, 30, 40, 50, 60, 71 | `Instance_b<puentes>_a<alojamientos>_d<días>[_s<semilla>].dat` |
| `instances_v3/` | 68 | 10, 20, 50, 205 | `Instance_d<familia>_b<puentes>_a<alojamientos>_d<días>[_s<semilla>].dat` |

Ambas usan el mismo formato AMPL/GLPK y el mismo programa; no hace falta ninguna opción especial para v3.

## Estructura

```
Makefile              build con un solo comando
src/                  código fuente (C++17)
  instance.*          parser y estructuras (Depot/Bridge/Lodging, matriz d densa)
  solution.*          representación + evaluate() (Ec. 1)
  constraints.hpp     verificadores independientes de Ec. 2, 3-4, 6-7, 8, 9
  initializer.*       construcción restringida + regla pseudo-aleatoria (Ec. 10)
  localsearch.*       2-opt, reubicación de puentes, eliminación de equipo
  acs.*               ACS (evaporación local, actualización global, traza, límite de tiempo)
  main.cpp            CLI
instances/            161 instancias originales
instances_v3/         68 instancias v3
scripts/
  benchmark.sh        corre todas las instancias de una carpeta con varias semillas -> CSV
  summarize.py        resume ese CSV por tamaño (mejora del ACS, equipos, tiempos)
  check_instances.py  detecta instancias inconsistentes o infactibles antes de correr
experiments/          experimentos del informe (exp.py, analyze.py, optuna_tune.py, exact.cpp) y resultados
```

## Requisitos y compilación (Ubuntu / WSL)

```bash
sudo apt install build-essential python3     # g++ con C++17 (probado con g++ 13.3, Ubuntu 24.04)
make                                         # genera ./mdbirp, sin advertencias
```

## Ejecutar una instancia

```bash
# Instancia del conjunto original
./mdbirp instances/Instance_b30_a10_d5_s555.dat

# Instancia v3 (pequeña, mediana y grande)
./mdbirp instances_v3/Instance_d2_b10_a4_d3_s137.dat
./mdbirp instances_v3/Instance_d5_b50_a12_d7_s129.dat
./mdbirp instances_v3/Instance_d1_b205_a21_d40_s129.dat     # 205 puentes: ~8 s
```

El programa imprime la solución inicial (greedy), la del ACS, las rutas por equipo y día, el desglose de costo y la validación de cada ecuación. **Termina con código 0 solo si la solución final pasa todos los verificadores.**

Salidas útiles para scripts:

```bash
./mdbirp instances_v3/Instance_d3_b20_a5_d3_s129.dat --seed 7 --csv        # una línea CSV
./mdbirp instances_v3/Instance_d3_b20_a5_d3_s129.dat --trace conv.csv      # convergencia por iteración
./mdbirp instances_v3/Instance_d3_b20_a5_d3_s129.dat --solution sol.csv    # rutas de la solución
./mdbirp instances_v3/Instance_d3_b20_a5_d3_s129.dat --iterations 0        # solo el greedy
```

## Ejecutar un conjunto completo (benchmark)

```bash
# 1) revisar las instancias (inconsistentes / infactibles)
python3 scripts/check_instances.py instances instances_v3

# 2) correr cada conjunto con 3 semillas (JOBS = procesos en paralelo)
JOBS=6 scripts/benchmark.sh instances    bench_out/old.csv 101 102 103
JOBS=6 scripts/benchmark.sh instances_v3 bench_out/v3.csv  101 102 103

# 3) resumen por tamaño
python3 scripts/summarize.py bench_out/old.csv bench_out/v3.csv
```

Con menos procesos en paralelo los tiempos son más fiables. Para cambiar parámetros: `EXTRA="--iterations 50 --ants 30" scripts/benchmark.sh ...`.

## Resultados de referencia (3 semillas: 101-103, parámetros por defecto)

Mejora del ACS sobre el greedy, equipos y tiempo por corrida (6 procesos en paralelo):

| Conjunto | b | Mejora ACS vs greedy | Tiempo medio |
|---|---|---|---|
| original | 10 | 22.5 % | 0.02 s |
| original | 20 | 13.4 % | 0.05 s |
| original | 50 | 14.7 % | 0.30 s |
| original | 60 | 12.9 % | 0.45 s |
| original | 71 | 4.0 % | 0.90 s |
| **original, total** | | **12.75 %** | 74 s en 483 corridas |
| v3 | 10 | 17.0 % | 0.01 s |
| v3 | 20 | 12.9 % | 0.04 s |
| v3 | 50 | 19.8 % | 0.41 s |
| v3 | 205 | 22.9 % | 8.3 s (máx. 11.7 s) |
| **v3, total** | | **17.25 %** | 202 s en 198 corridas |

Los resultados completos están en `results/`: `instances_original.csv` y `instances_v3.csv` (una fila por instancia y semilla, con costo greedy y final, equipos, tiempo y validez), `resumen.txt` (las tablas de arriba), `check_instances.txt` y `entorno.txt` (CPU, compilador, parámetros). Se regeneran con los comandos de la sección *Ejecutar un conjunto completo*; con la misma semilla los costos son idénticos, los tiempos varían según la máquina.

Los costos absolutos no son comparables entre ambos conjuntos (escalas de datos distintas); sí lo son la mejora relativa y los tiempos. Las 483 corridas del conjunto original y las 198 válidas de v3 pasan todos los verificadores.

## Problemas conocidos en `instances_v3/`

`scripts/check_instances.py` detecta dos archivos defectuosos (son problemas de los datos, no del código). Están incluidos tal cual, pero el programa los rechaza:

| Archivo | Problema |
|---|---|
| `Instance_d2_b10_a4_d3_s129.dat` | **Infactible**: el puente 84 necesita al menos 555 min en un día y Q = 540. |
| `Instance_d1_b205_a21_d40_s574.dat` | **Inconsistente**: declara el depósito 228 en `set O`, pero sus coordenadas y distancias usan el 231. |

Las 66 instancias restantes funcionan. `benchmark.sh` registra estas corridas como `FAIL` y continúa con las demás.

## Opciones

| Flag | Default | Descripción |
|------|---------|-------------|
| `--seed N` | 42 | Semilla aleatoria |
| `--alpha F` | 1.55 | Exponente de τ (Ec. 10) |
| `--beta F` | 3.59 | Exponente de η (Ec. 10) |
| `--rho F` | 0.5 | Evaporación global |
| `--xi F` | 0.26 | Evaporación local |
| `--q0 F` | 0.64 | Parámetro q0 de la Ec. 10, en [0,1] (la inicialización greedy usa q0 = 1) |
| `--ants N` | 40 | Hormigas por iteración |
| `--iterations N` | 25 | Iteraciones ACS; `0` = solo inicialización greedy |
| `--time S` | 0 | Límite de tiempo del ACS en segundos; `0` = sin límite |
| `--ls-moves M` | relocate | Búsqueda local: `none`, `2opt`, `relocate`, `eliminate` o `all` |
| `--ls-prob F` | 0.25 | Probabilidad de aplicar la búsqueda local en una iteración, en [0,1] |
| `--ls-iters N` | 30 | Rondas de mejora como máximo por aplicación |
| `--csv` | — | Una sola línea: `instancia,semilla,costo_greedy,costo_final,equipos,iteraciones,segundos,valida,equipos_greedy,viaje,despacho,alojamiento,noches,cota_equipos` |
| `--trace archivo` | — | CSV con el costo de la mejor solución por iteración |
| `--solution archivo` | — | CSV con las rutas de la solución final |
| `--teams-per-depot N` | 1 | Equipos por depósito |
| `--total-teams N` | — | Equipos en total (sobreescribe `--teams-per-depot`) |

Los valores por defecto son la configuración sintonizada (`experiments/results/tuned_config.json`; 40 hormigas × 25 iteraciones = 1000 construcciones). Los equipos pedidos son una cota inferior: si no alcanzan para cubrir todos los puentes se despachan más.

**Hormigas sin salida:** con instancias grandes una hormiga puede dejar un puente que ningún equipo nuevo alcanza dentro de Q. En ese caso la hormiga se descarta (`src/acs.cpp`) y el ACS continúa; la mejor solución parte siendo la del greedy, así que el resultado nunca es peor que ella.

## Algoritmo: criterios de aceptación y de término

Cada iteración, `--ants` hormigas construyen una solución completa con la Ec. 10. A la mejor de ellas se le aplica la búsqueda local (con probabilidad `--ls-prob`). Después:

- **Criterio de aceptación:** la mejor solución global se reemplaza solo si la candidata es estrictamente más barata. La búsqueda local acepta un movimiento solo si la solución sigue siendo factible y el costo de la Ec. 1 baja.
- **Actualización global de feromona:** los arcos de la mejor solución global reciben `rho / costo`.
- **Criterio de término:** `--iterations` iteraciones o `--time` segundos, lo que ocurra primero.

### Búsqueda local

Se aplica a la mejor hormiga de cada iteración y mantiene la factibilidad (arcos existentes, turno ≤ Q, cadena de alojamientos y depósito):

- `2opt`: invierte un tramo de puentes dentro de un día. Inicio y fin del día no cambian.
- `relocate`: mueve un puente a otra posición, día o equipo. Si un día queda vacío se elimina y se reconecta la cadena de alojamientos.
- `eliminate`: intenta repartir todos los puentes de un equipo en las rutas de los demás. Si caben, el equipo desaparece y se ahorra su despacho `v`.

### Salidas para experimentos

```bash
./mdbirp instances/Instance_b30_a10_d5_s555.dat --seed 3 --csv
./mdbirp instances/Instance_b30_a10_d5_s555.dat --trace convergencia.csv
./mdbirp instances/Instance_b30_a10_d5_s555.dat --solution solucion.csv
```

## Reproducir los experimentos

`experiments/exp.py` (Python 3, solo biblioteca estándar, corre en Ubuntu) ejecuta todas las etapas y deja los resultados en `experiments/results/`:

```bash
make
python3 experiments/exp.py all          # hardware, subset, tune, calibrate, evaluate, traces
python3 experiments/exp.py all --quick  # versión reducida para probar el flujo
python3 experiments/exp.py ablation     # ablación de operadores (aparte de 'all')
python3 experiments/exp.py budget       # sensibilidad al presupuesto en b25 y b60
python3 experiments/optuna_tune.py 81   # sintonización con Optuna (opcional; en WSL ajustar WSL_EXP)
```

| Etapa | Qué hace |
|---|---|
| `hardware` | Registra CPU, memoria, sistema y compilador. |
| `subset` | Elige el 40 % de las instancias de cada tamaño para sintonizar (`subset_tuning.txt`, fijo, semilla 2026). b71 no se usa para sintonizar. |
| `tune` | Búsqueda aleatoria (60 configuraciones) y refinamiento (20 vecinas) sobre ese subset, con las semillas 1 a 5 y el mismo cómputo por configuración (hormigas × iteraciones = 1000). Minimiza el costo final relativo al greedy de referencia (parámetros por defecto). |
| `calibrate` | Mide k = tiempo medio con búsqueda local / tiempo medio sin ella, en el subset. |
| `evaluate` | Las 161 instancias con las semillas 101 a 120 en cuatro variantes: greedy, ACS, ACS con búsqueda local y ACS sin búsqueda local con k veces las iteraciones (mismo cómputo). Reanuda si se interrumpe. |
| `ablation` | Operadores de búsqueda local por separado (none, 2opt, relocate, all) con la configuración sintonizada, todas las instancias y las semillas 101-120. |
| `budget` | Sensibilidad al presupuesto: b25 y b60 con 10 veces las iteraciones, semillas 101-120 (aparte de `all`). |
| `traces` | Convergencia y soluciones de una instancia por tamaño, con todas las semillas. |

El análisis (tablas, pruebas de Wilcoxon pareadas con corrección de Holm, boxplots, convergencia y mapas de soluciones) se hace con `python experiments/analyze.py` (dependencias en `experiments/requirements.txt`: `pip install -r experiments/requirements.txt`) y se escribe en `experiments/results/analysis/`.

### Óptimo exacto (b10 y b15)

`experiments/exact.cpp` calcula el óptimo por programación dinámica sobre subconjuntos de puentes, con el mismo modelo que el verificador (días no vacíos, a lo más |K| días, turno ≤ Q, último día de vuelta al depósito de origen, sin límite de equipos). Sirve para medir la brecha real del ACS en las familias de 10 y 15 puentes (con 20 puentes o más no es tratable):

```bash
g++ -std=c++17 -O2 -Isrc -o exact experiments/exact.cpp src/instance.cpp
./exact instances/Instance_b10_a4_d3_s129.dat   # imprime instancia,costo_óptimo,equipos
```

Los óptimos de las 40 instancias están en `experiments/results/exact.csv`.

El término en los experimentos es siempre por iteraciones, no por tiempo: `--time` depende de la carga de la máquina y rompe la reproducibilidad con la misma semilla.

## Fidelidad al modelo

- Grafo de construcción G_C: bridges, depósitos y alojamientos son nodos; las feromonas τ viven sobre los arcos (matriz `Pheromone`).
- Una hormiga construye la ruta de **un** equipo, día a día (`Builder::build_route`); una solución es el conjunto de rutas construidas en secuencia sobre el mismo conjunto de puentes pendientes.
- La regla pseudo-aleatoria (Ec. 10) elige tanto el **siguiente puente** (η = 1/d) como el **alojamiento** donde se pernocta (η = 1/(d + tarifa), el costo inmediato de esa decisión según la Ec. 1). Las tarifas son del orden de decenas de miles y las distancias de cientos, así que en la práctica el alojamiento se elige por tarifa entre los factibles: no hay un factor de escala λ entre ambos términos. Queda sin modelar que la distancia al alojamiento condiciona el turno del día siguiente; solo lo cubre el filtro de factibilidad. El regreso al depósito ocurre cuando ya no hay día disponible.
- Simplificaciones (declaradas): el alojamiento se considera solo al cerrar el día (no se interrumpe un día con espacio útil), y solo entre alojamientos "viables" (desde los que aún se puede atender algún puente pendiente y volver al depósito); además hay una reserva de tiempo conservadora para no quedar varado. La inicialización usa τ uniforme y q0 = 1.

## Cómo se garantiza la factibilidad (codificación restringida)

La hormiga solo escoge dentro de N_i^k, así que la solución es válida por construcción:

| Ecuación | Mecanismo |
|---|---|
| Ec. 2 cobertura | lista de puentes pendientes; se elimina al visitar |
| Ec. 9 turno Q | solo son candidatos los puentes que caben en el turno restante **más una reserva** para cerrar el día en un alojamiento viable o en el depósito |
| Ec. 6-7 lodging | al cortar el día se fija el alojamiento m (viable: puede servir aún algún puente); el día k+1 parte exactamente de m |
| Ec. 3-4, 8 depósito | el depósito se fija al crear el equipo; el último día vuelve a él |

`constraints.hpp` recalcula todo desde las rutas (arcos y tiempos) sin usar valores cacheados, como prueba independiente.

## Salida

```
Instancia: instances/Instance_b15_a5_d3_s129.dat
  15 puentes, 2 depósitos, 5 alojamientos, 3 días, turno Q = 540 min
Parámetros: semilla 7, alpha 1, beta 2, ...

Solución inicial (greedy, Ec.10 con q0=1): costo 4146926, 2 equipo(s)
Solución ACS: costo 4056602 (2.2% menos que la inicial), 0.02 s

Solución final:
Equipo 0 (depósito 0)
  Día 1: puentes 33 62 63 37 84 -> duerme en alojamiento 137 (524/540 min)
  ...

Costo = viaje 890 + despacho 4000000 + alojamiento 55712 = 4056602

Validación:
  [OK]    Ec.2     cada puente se visita una vez
  [OK]    Ec.9     turno diario <= Q
  [OK]    Ec.6-7   cada día parte donde terminó el anterior
  [OK]    Ec.3-4,8 un depósito por equipo, y vuelve a él
=> SOLUCIÓN VÁLIDA
```

## Verificación realizada

- Greedy y ACS sobre las 161 instancias, con varias semillas y configuraciones de equipos y de búsqueda local: 0 soluciones inválidas. La corrida final (161 instancias × 20 semillas × 4 variantes, 12.880 corridas) no tuvo ninguna inválida.
- Con los parámetros por defecto, el ACS mejora en promedio 12,8 % el costo del greedy (la mediana por corrida es 1,5 %, porque la mejora grande ocurre cuando se ahorra un equipo); tiempo medio 0,18 s y máximo 2,2 s por corrida.
- La misma semilla reproduce el mismo resultado (30 corridas comparadas contra `eval_runs.csv`, 0 diferencias).
- El validador recalcula todo desde las rutas, incluido el costo de la Ec. 1.
- Los resultados de los experimentos (sintonización, evaluación, ablación de operadores, pruebas estadísticas) están en `experiments/results/`; las tablas y figuras se regeneran con `python experiments/analyze.py`.
