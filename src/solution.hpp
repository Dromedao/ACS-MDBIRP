#pragma once
#include "instance.hpp"
#include <vector>

// Estructuras de la solución
// Correspondencia con el modelo matemático:
//
//   x_{ij}^{e,k}  : arcos implícitos en el orden de DaySegment::bridges_visited
//                   (cada par consecutivo es un arco usado)
//   y_o^e         : TeamRoute::depot_id  (equipo e despachado desde o)
//   z_m^{e,k}     : DaySegment::lodging_end  (equipo e pernocta en m al final del día k)
//                   -1 si el equipo no pernocta (último día, regresa al depot)

// Representación de la solución: una solución es una lista de rutas, una
// por equipo (Solution -> TeamRoute). Cada ruta tiene el depósito de origen y
// una lista de días (DaySegment); cada día guarda los puentes en el orden en
// que se visitan y el alojamiento donde se duerme (-1 el último día, que vuelve
// al depósito). De ahí se deducen todos los arcos usados, el costo (Ec. 1) y las
// restricciones, sin guardar variables binarias.
struct DaySegment {  // PAUTA representación de la solución
    // IDs de los puentes visitados ese día, en orden de visita.
    // El día comienza desde el punto de partida (depot o alojamiento del día anterior)
    // y termina en lodging_end o en el depot de origen.
    std::vector<int> bridges_visited;

    // ID del alojamiento donde pernocta el equipo al final de este día.
    // -1 si es el último día activo (el equipo regresa al depot).
    int lodging_end = -1;

    // Caches rellenados por evaluate(): solo para imprimir/depurar. Los
    // verificadores de constraints.hpp NO los usan: recalculan desde la ruta.
    double travel_time = 0.0;   // suma de d_ij del día
    double work_time   = 0.0;   // travel_time + sum(w_j): debe ser <= Q (Ec. 9)
};

struct TeamRoute {
    int team_id;
    int depot_id;                      // y_o^e: depósito de origen y retorno

    // Un segmento por día activo. days[0] = día 1, days[1] = día 2, etc.
    std::vector<DaySegment> days;

    bool is_empty() const { return days.empty(); }
};

struct Solution {
    std::vector<TeamRoute> routes;   // una por equipo despachado

    // Costo total: calculado por evaluate() usando exactamente la Ec. (1)
    double total_cost = 0.0;

    // Desglose de la Ec. (1): (a) viaje, (b) despacho, (c) alojamiento
    double travel_cost = 0.0, dispatch_cost = 0.0, lodging_cost = 0.0;
};

// Recorre los arcos (i -> j) usados por un día de un equipo, en orden:
//   inicio -> b1 -> b2 -> ... -> bn -> fin
// inicio = depósito (día 1) o alojamiento del día previo; fin = lodging_end del
// día o el depósito (último día). `start` es el nodo de partida del día.
// Un día sin puentes y con alojamiento genera el arco start -> lodging_end.
template <class F>
void for_each_arc(const TeamRoute& route, size_t day_idx, int start, F&& f) {
    const DaySegment& seg = route.days[day_idx];
    int cur = start;
    for (int b : seg.bridges_visited) { f(cur, b); cur = b; }
    int end = (seg.lodging_end != -1) ? seg.lodging_end : route.depot_id;
    if (cur != end) f(cur, end);
}

// Nodo donde empieza el día `day_idx` (Ec. 4 para el día 1; Ec. 7 para k+1).
inline int day_start_node(const TeamRoute& route, size_t day_idx) {
    return day_idx == 0 ? route.depot_id : route.days[day_idx - 1].lodging_end;
}

// Función de evaluación: implementa la Ec. (1) del modelo:
//
//   min  sum_{e,k} sum_{(i,j)} d_{ij} * x_{ij}^{e,k}    (costo de viaje)
//      + v * sum_{e,o} y_o^e                              (costo de despacho)
//      + sum_{e,k,m} f_m * z_m^{e,k}                     (costo de alojamiento)
//
// Retorna el costo total y lo escribe en sol.total_cost.
double evaluate(Solution& sol, const Instance& inst);  // PAUTA función objetivo (Ec. 1)

// Imprime un resumen legible de la solución (para depurar).
void print_solution(const Solution& sol, const Instance& inst);
