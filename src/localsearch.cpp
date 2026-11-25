#include "localsearch.hpp"

#include <algorithm>

namespace {

const double EPS = 1e-6;      // tolerancia numérica del turno (igual que en la construcción)
const double GAIN = 1e-9;     // mejora mínima para aceptar un movimiento

// Costo de una ruta (despacho + viaje + alojamiento) si cumple el modelo.
// Devuelve false cuando no es factible. Revisa lo mismo que constraints.hpp:
// al menos un puente por día, todos los arcos existen, turno <= Q, solo el
// último día vuelve al depósito y los demás terminan en un alojamiento.
bool route_cost(const TeamRoute& r, const Instance& inst, double& cost) {  // PAUTA operadores: solo vecinos factibles
    if (r.days.empty() || r.days.size() > inst.days.size()) return false;
    double c = inst.v;
    for (size_t k = 0; k < r.days.size(); ++k) {
        const DaySegment& s = r.days[k];
        const bool last = (k + 1 == r.days.size());
        if (s.bridges_visited.empty()) return false;
        if (last != (s.lodging_end == -1)) return false;

        double work = 0.0;
        bool ok = true;
        for_each_arc(r, k, day_start_node(r, k), [&](int i, int j) {
            if (!ok) return;
            if (!inst.has_arc(i, j)) { ok = false; return; }
            double dd = inst.get_dist(i, j);
            work += dd;
            c += dd;
            if (const Bridge* b = inst.find_bridge(j)) work += b->w;
        });
        if (!ok || work > inst.Q + EPS) return false;
        if (!last) c += inst.get_lodging_rate(s.lodging_end);
    }
    cost = c;
    return true;
}

// Quita el puente en la posición p del día k. Si el día queda vacío, lo elimina
// y reconecta la cadena de alojamientos (el día anterior pasa a terminar donde
// terminaba el eliminado). Devuelve true si la ruta completa quedó vacía.
bool remove_bridge(TeamRoute& r, size_t k, size_t p) {
    auto& v = r.days[k].bridges_visited;
    v.erase(v.begin() + p);
    if (!v.empty()) return false;

    if (r.days.size() == 1) { r.days.clear(); return true; }
    const int end = r.days[k].lodging_end;
    r.days.erase(r.days.begin() + k);
    if (k > 0) r.days[k - 1].lodging_end = end;   // -1 si era el último día
    return false;
}

// Operador relocate (--ls-moves relocate): reubica un puente, a otra posición,
// otro día u otro equipo. Prueba todos los destinos y aplica el primer movimiento
// que baja el costo. Devuelve true si aplicó alguno.
bool relocate_once(Solution& sol, const Instance& inst) {  // PAUTA operador relocate
    for (size_t r1 = 0; r1 < sol.routes.size(); ++r1) {
        double old1;
        if (!route_cost(sol.routes[r1], inst, old1)) continue;

        for (size_t k1 = 0; k1 < sol.routes[r1].days.size(); ++k1)
            for (size_t p1 = 0; p1 < sol.routes[r1].days[k1].bridges_visited.size(); ++p1) {
                const int bid = sol.routes[r1].days[k1].bridges_visited[p1];

                TeamRoute a = sol.routes[r1];
                const bool deleted = remove_bridge(a, k1, p1);
                double cost_a = 0.0;
                const bool a_ok = deleted || route_cost(a, inst, cost_a);

                // Destinos dentro de la misma ruta (ya sin el puente)
                for (size_t k2 = 0; k2 < a.days.size(); ++k2) {
                    auto& v = a.days[k2].bridges_visited;
                    for (size_t p2 = 0; p2 <= v.size(); ++p2) {
                        v.insert(v.begin() + p2, bid);
                        double c;
                        const bool ok = route_cost(a, inst, c);
                        if (ok && c < old1 - GAIN) { sol.routes[r1] = a; return true; }
                        v.erase(v.begin() + p2);
                    }
                }

                // Destinos en otros equipos
                if (!a_ok) continue;
                for (size_t r2 = 0; r2 < sol.routes.size(); ++r2) {
                    if (r2 == r1) continue;
                    double old2;
                    if (!route_cost(sol.routes[r2], inst, old2)) continue;

                    for (size_t k2 = 0; k2 < sol.routes[r2].days.size(); ++k2) {
                        auto& v = sol.routes[r2].days[k2].bridges_visited;
                        for (size_t p2 = 0; p2 <= v.size(); ++p2) {
                            v.insert(v.begin() + p2, bid);
                            double c2;
                            const bool ok = route_cost(sol.routes[r2], inst, c2);
                            if (ok && cost_a + c2 < old1 + old2 - GAIN) {
                                if (deleted) sol.routes.erase(sol.routes.begin() + r1);
                                else         sol.routes[r1] = a;
                                return true;
                            }
                            v.erase(v.begin() + p2);
                        }
                    }
                }
            }
    }
    return false;
}

// Operador eliminate (--ls-moves eliminate): intenta eliminar un equipo repartiendo
// sus puentes en las demás rutas, cada uno en la posición factible más barata.
// Prueba primero los equipos con menos puentes y acepta solo si el costo baja.
bool eliminate_once(Solution& sol, const Instance& inst) {  // PAUTA operador eliminate
    if (sol.routes.size() < 2) return false;

    std::vector<size_t> order(sol.routes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    auto n_bridges = [&](size_t r) {
        size_t n = 0;
        for (const auto& d : sol.routes[r].days) n += d.bridges_visited.size();
        return n;
    };
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b) { return n_bridges(a) < n_bridges(b); });

    for (size_t r : order) {
        double old_total = 0.0;
        std::vector<TeamRoute> others;
        std::vector<double> cost;
        bool valid = true;
        for (size_t i = 0; i < sol.routes.size(); ++i) {
            double c;
            if (!route_cost(sol.routes[i], inst, c)) { valid = false; break; }
            old_total += c;
            if (i != r) { others.push_back(sol.routes[i]); cost.push_back(c); }
        }
        if (!valid) return false;

        std::vector<int> pending;
        for (const auto& d : sol.routes[r].days)
            for (int b : d.bridges_visited) pending.push_back(b);

        bool ok = true;
        for (int bid : pending) {
            double best_delta = 1e300, best_cost = 0.0;
            size_t bi = 0, bk = 0, bp = 0;
            bool found = false;
            for (size_t i = 0; i < others.size(); ++i)
                for (size_t k = 0; k < others[i].days.size(); ++k) {
                    auto& v = others[i].days[k].bridges_visited;
                    for (size_t p = 0; p <= v.size(); ++p) {
                        v.insert(v.begin() + p, bid);
                        double c;
                        if (route_cost(others[i], inst, c) && c - cost[i] < best_delta) {
                            best_delta = c - cost[i]; best_cost = c;
                            bi = i; bk = k; bp = p; found = true;
                        }
                        v.erase(v.begin() + p);
                    }
                }
            if (!found) { ok = false; break; }
            auto& v = others[bi].days[bk].bridges_visited;
            v.insert(v.begin() + bp, bid);
            cost[bi] = best_cost;
        }
        if (!ok) continue;

        double new_total = 0.0;
        for (double c : cost) new_total += c;
        if (new_total < old_total - GAIN) {
            sol.routes = others;
            return true;
        }
    }
    return false;
}

// Operador 2-opt (--ls-moves 2opt): invierte el tramo [i, j] de un día si baja el costo.
// Repite hasta que no haya mejora y devuelve true si cambió algo.
bool two_opt_pass(Solution& sol, const Instance& inst) {  // PAUTA operador 2-opt
    bool any = false;
    for (auto& route : sol.routes) {
        double best;
        if (!route_cost(route, inst, best)) continue;

        for (auto& day : route.days) {
            auto& v = day.bridges_visited;
            bool improved = true;
            while (improved) {
                improved = false;
                for (size_t i = 0; i + 1 < v.size() && !improved; ++i)
                    for (size_t j = i + 1; j < v.size(); ++j) {
                        std::reverse(v.begin() + i, v.begin() + j + 1);
                        double c;
                        if (route_cost(route, inst, c) && c < best - GAIN) {
                            best = c;
                            improved = any = true;
                            break;
                        }
                        std::reverse(v.begin() + i, v.begin() + j + 1);
                    }
            }
        }
    }
    return any;
}

}  // namespace

// Punto de entrada de la búsqueda local. Parametrizada por LsParams: p.moves
// elige los vecindarios (--ls-moves) y p.max_rounds la intensidad (--ls-iters).
// Criterio de aceptación de cada movimiento: solo si route_cost() lo declara
// factible y el costo baja en más de GAIN; si no, se deshace.
int local_search(Solution& sol, const Instance& inst, const LsParams& p) {  // PAUTA operadores: aceptación de movimientos
    int rounds = 0;
    while (rounds < p.max_rounds) {
        bool moved = false;
        if (p.moves & LS_ELIMINATE) moved = eliminate_once(sol, inst);
        if (p.moves & LS_RELOCATE) moved = relocate_once(sol, inst) || moved;
        if (p.moves & LS_2OPT)     moved = two_opt_pass(sol, inst) || moved;
        if (!moved) break;
        ++rounds;
    }
    if (rounds > 0) evaluate(sol, inst);
    return rounds;
}
