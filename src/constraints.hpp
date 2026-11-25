#pragma once
#include "instance.hpp"
#include "solution.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Verificadores de restricciones. La construcción (initializer.cpp) ya deja las
// soluciones factibles; estas funciones lo comprueban de nuevo recalculando todo
// desde las rutas, sin usar los valores guardados en DaySegment. Cada una
// devuelve true si se cumple y, con verbose, imprime el primer fallo.
// Cubren cobertura (Ec. 2), turno (Ec. 9), alojamientos (Ec. 6-7), depósito
// (Ec. 3-4 y 8) y costo (Ec. 1).

// Ec. (2): cada puente de N aparece exactamente una vez en el conjunto de rutas
inline bool check_bridge_coverage(const Solution& sol, const Instance& inst, bool verbose = true) {  // PAUTA restricciones: los verificadores
    std::unordered_map<int, int> count;
    for (const auto& b : inst.bridges) count[b.id] = 0;

    bool ok = true;
    for (const auto& route : sol.routes)
        for (const auto& seg : route.days)
            for (int bid : seg.bridges_visited) {
                auto it = count.find(bid);
                if (it == count.end()) {
                    if (verbose) std::cerr << "  [Ec.2] Nodo " << bid << " visitado pero no es un puente\n";
                    ok = false;
                } else {
                    it->second++;
                }
            }
    for (const auto& kv : count)
        if (kv.second != 1) {
            if (verbose)
                std::cerr << "  [Ec.2] Puente " << kv.first << " visitado " << kv.second
                          << " veces (debe ser 1)\n";
            ok = false;
        }
    return ok;
}

// Ec. (9): sum (d_ij + w_j) de los arcos del día k <= Q, recalculado desde cero.
// Además exige que el equipo use a lo sumo |K| días y que todos los arcos existan.
inline bool check_shift_limits(const Solution& sol, const Instance& inst, bool verbose = true) {
    bool ok = true;
    for (const auto& route : sol.routes) {
        if (route.days.size() > inst.days.size()) {
            if (verbose)
                std::cerr << "  [Ec.9] Equipo " << route.team_id << " usa " << route.days.size()
                          << " días > |K|=" << inst.days.size() << "\n";
            ok = false;
        }
        for (size_t k = 0; k < route.days.size(); ++k) {
            double work = 0.0;
            bool arcs_ok = true;
            for_each_arc(route, k, day_start_node(route, k), [&](int i, int j) {
                if (!inst.has_arc(i, j)) { arcs_ok = false; return; }
                work += inst.get_dist(i, j);
                if (const Bridge* b = inst.find_bridge(j)) work += b->w;   // w_j de cada puente
            });
            if (!arcs_ok) {
                if (verbose)
                    std::cerr << "  [Ec.9] Equipo " << route.team_id << " día " << (k + 1)
                              << " usa un arco que no existe en la instancia\n";
                ok = false;
            } else if (work > inst.Q + 1e-6) {
                if (verbose)
                    std::cerr << "  [Ec.9] Equipo " << route.team_id << " día " << (k + 1)
                              << ": turno=" << work << " > Q=" << inst.Q << "\n";
                ok = false;
            }
        }
    }
    return ok;
}

// Ec. (6)-(7): el alojamiento donde termina el día k existe, y el día k+1 parte
// exactamente de él (day_start_node). Todo día no final termina en alojamiento
// y el siguiente día no puede estar vacío (llegar a m y no salir no tiene sentido).
inline bool check_lodging_links(const Solution& sol, const Instance& inst, bool verbose = true) {
    bool ok = true;
    for (const auto& route : sol.routes) {
        for (size_t k = 0; k < route.days.size(); ++k) {
            const auto& seg = route.days[k];
            bool is_last = (k + 1 == route.days.size());

            if (!is_last && seg.lodging_end == -1) {
                if (verbose)
                    std::cerr << "  [Ec.6-7] Equipo " << route.team_id << " día " << (k + 1)
                              << " no pernocta pero hay día siguiente\n";
                ok = false;
            }
            if (seg.lodging_end != -1 && inst.find_lodging(seg.lodging_end) == nullptr) {
                if (verbose)
                    std::cerr << "  [Ec.6-7] Equipo " << route.team_id << " día " << (k + 1)
                              << ": lodging_end=" << seg.lodging_end << " no es un alojamiento\n";
                ok = false;
            }
            if (k > 0) {
                int start = day_start_node(route, k);
                if (inst.find_lodging(start) == nullptr || seg.bridges_visited.empty() ||
                    !inst.has_arc(start, seg.bridges_visited.front())) {
                    if (verbose)
                        std::cerr << "  [Ec.7] Equipo " << route.team_id << " día " << (k + 1)
                                  << " no parte con un arco real desde el alojamiento " << start << "\n";
                    ok = false;
                }
            }
        }
    }
    return ok;
}

// Ec. (3)-(4) y (8): cada equipo usa un único depósito válido, sale de él el
// día 1, y vuelve exactamente una vez a él (el último día, sin pernoctar).
// Además, cada team_id es único (un equipo = un depósito).
inline bool check_depot_assignment(const Solution& sol, const Instance& inst, bool verbose = true) {
    bool ok = true;
    std::unordered_set<int> seen_team;
    for (const auto& route : sol.routes) {
        if (!seen_team.insert(route.team_id).second) {
            if (verbose) std::cerr << "  [Ec.3] Equipo " << route.team_id << " repetido\n";
            ok = false;
        }
        if (inst.find_depot(route.depot_id) == nullptr) {
            if (verbose)
                std::cerr << "  [Ec.3-4] Equipo " << route.team_id << " tiene depot_id="
                          << route.depot_id << " inexistente\n";
            ok = false;
        }
        if (route.days.empty()) {
            if (verbose) std::cerr << "  [Ec.3] Equipo " << route.team_id << " despachado sin ruta\n";
            ok = false;
            continue;
        }
        if (route.days.front().bridges_visited.empty() ||
            !inst.has_arc(route.depot_id, route.days.front().bridges_visited.front())) {
            if (verbose)
                std::cerr << "  [Ec.4] Equipo " << route.team_id
                          << " no sale del depósito el día 1 por un arco real\n";
            ok = false;
        }
        const auto& last = route.days.back();
        if (last.lodging_end != -1 || last.bridges_visited.empty() ||
            !inst.has_arc(last.bridges_visited.back(), route.depot_id)) {
            if (verbose)
                std::cerr << "  [Ec.8] Equipo " << route.team_id
                          << " no regresa a su depósito al final\n";
            ok = false;
        }
    }
    return ok;
}

// Ec. (1): recalcula el costo desde las rutas, sin usar los valores guardados en la
// solución, y comprueba que total_cost coincide con él.
inline bool check_cost(const Solution& sol, const Instance& inst, bool verbose = true) {
    double travel = 0.0, lodging = 0.0;
    for (const auto& route : sol.routes) {
        for (size_t k = 0; k < route.days.size(); ++k) {
            for_each_arc(route, k, day_start_node(route, k),
                         [&](int i, int j) { travel += inst.get_dist(i, j); });
            if (route.days[k].lodging_end != -1) lodging += inst.get_lodging_rate(route.days[k].lodging_end);
        }
    }
    double total = travel + inst.v * (double)sol.routes.size() + lodging;
    bool ok = std::fabs(total - sol.total_cost) <= 1e-6 * std::max(1.0, total);
    if (!ok && verbose)
        std::cerr << "  [Ec.1] costo recalculado " << total << " distinto del guardado " << sol.total_cost << "\n";
    return ok;
}

// Ejecuta todos los checks e imprime [OK]/[ERROR] por ecuación.
inline bool validate_solution(const Solution& sol, const Instance& inst) {  // PAUTA restricciones: validación de toda la solución
    bool cov  = check_bridge_coverage(sol, inst);
    bool shft = check_shift_limits(sol, inst);
    bool lnk  = check_lodging_links(sol, inst);
    bool dep  = check_depot_assignment(sol, inst);
    bool cst  = check_cost(sol, inst);

    auto tag = [](bool b, const char* name) {
        std::cout << "  " << (b ? "[OK]    " : "[ERROR] ") << name << "\n";
    };
    std::cout << "\nValidación:\n";
    tag(cov,  "Ec.2     cada puente se visita una vez");
    tag(shft, "Ec.9     turno diario <= Q");
    tag(lnk,  "Ec.6-7   cada día parte donde terminó el anterior");
    tag(dep,  "Ec.3-4,8 un depósito por equipo, y vuelve a él");
    tag(cst,  "Ec.1     el costo coincide con el recalculado desde las rutas");

    bool all_ok = cov && shft && lnk && dep && cst;
    std::cout << (all_ok ? "=> SOLUCIÓN VÁLIDA\n" : "=> SOLUCIÓN INVÁLIDA\n");
    return all_ok;
}
