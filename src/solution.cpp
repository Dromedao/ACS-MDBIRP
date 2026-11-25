#include "solution.hpp"
#include <iostream>
#include <iomanip>

// evaluate(): implementa exactamente la Ec. (1) del modelo
//
//   C = sum_{e,k,(i,j)} d_{ij} * x_{ij}^{e,k}    (término 1: viaje)
//     + v * |{e : y_o^e = 1}|                      (término 2: despacho)
//     + sum_{e,k,m} f_m * z_m^{e,k}               (término 3: alojamiento)
double evaluate(Solution& sol, const Instance& inst) {
    double travel_cost   = 0.0;
    double dispatch_cost = 0.0;
    double lodging_cost  = 0.0;

    for (auto& route : sol.routes) {
        // Término 2: costo de despacho (v * y_o^e)
        dispatch_cost += inst.v;

        for (size_t k = 0; k < route.days.size(); ++k) {
            DaySegment& seg = route.days[k];
            seg.travel_time = 0.0;
            seg.work_time   = 0.0;

            // Término 1: costo de viaje, sum d_ij * x_ij^{e,k}
            for_each_arc(route, k, day_start_node(route, k), [&](int i, int j) {
                seg.travel_time += inst.get_dist(i, j);
            });
            seg.work_time = seg.travel_time;
            // El tiempo de inspección w_j entra al turno (Ec. 9) pero no al costo.
            for (int b : seg.bridges_visited) seg.work_time += inst.find_bridge(b)->w;

            travel_cost += seg.travel_time;

            // Término 3: costo de alojamiento, sum f_m * z_m^{e,k}
            if (seg.lodging_end != -1) lodging_cost += inst.get_lodging_rate(seg.lodging_end);
        }
    }

    sol.travel_cost   = travel_cost;
    sol.dispatch_cost = dispatch_cost;
    sol.lodging_cost  = lodging_cost;
    sol.total_cost    = travel_cost + dispatch_cost + lodging_cost;
    return sol.total_cost;
}

// print_solution(): muestra la solución de forma legible
void print_solution(const Solution& sol, const Instance& inst) {
    std::cout << std::fixed << std::setprecision(0);

    for (const auto& route : sol.routes) {
        std::cout << "Equipo " << route.team_id << " (depósito " << route.depot_id << ")\n";

        for (size_t di = 0; di < route.days.size(); ++di) {
            const DaySegment& seg = route.days[di];
            std::cout << "  Día " << (di + 1) << ": puentes";
            for (int b : seg.bridges_visited) std::cout << " " << b;
            if (seg.lodging_end != -1)
                std::cout << " -> duerme en alojamiento " << seg.lodging_end;
            else
                std::cout << " -> vuelve al depósito " << route.depot_id;
            std::cout << " (" << seg.work_time << "/" << inst.Q << " min)\n";
        }
    }

    std::cout << "\nCosto = viaje " << sol.travel_cost
              << " + despacho " << sol.dispatch_cost
              << " + alojamiento " << sol.lodging_cost
              << " = " << sol.total_cost << "\n";
}
