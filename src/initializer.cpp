#include "initializer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

const double EPS = 1e-6;

// Candidato de la regla de transición: id del nodo (puente o alojamiento) y su
// puntaje tau^alpha * eta^beta, con eta = 1/distancia. Mayor puntaje, más atractivo.
struct Cand { int bid; double score; };

// Estado de construcción compartido por todos los equipos de una hormiga.
// La hormiga lleva en su estado el día actual, el turno restante y el
// alojamiento actual.
struct Builder {
    const Instance&        inst;
    const ConstructParams& cp;
    Pheromone&             tau;
    bool                   local_update;
    std::mt19937&          rng;

    std::vector<int> unv;                 // ids de puentes por visitar (se vacía al visitar: Ec. 2)
    int n_days;                           // |K|: máximo de días por equipo
    int n_lod;                            // cantidad de alojamientos

    // Para saber si un alojamiento es "viable" (no deja varado al equipo).
    // Por depósito o: fresh_ok[o][m*nb + b] = 1 si, partiendo fresco (turno Q)
    // del alojamiento m, el puente b cabe y se puede volver a ese depósito.
    // cnt0[o][m] = nº de puentes que cumplen eso. Un alojamiento es viable
    // mientras desde él aún se pueda atender algún puente pendiente.
    std::vector<std::vector<char>> fresh_ok;
    std::vector<std::vector<int>>  cnt0;

    Builder(const Instance& i, const ConstructParams& c, Pheromone& t, bool lu, std::mt19937& r)
        : inst(i), cp(c), tau(t), local_update(lu), rng(r) {
        n_days = (int)inst.days.size();
        n_lod  = (int)inst.lodgings.size();
        for (const auto& b : inst.bridges) unv.push_back(b.id);

        int nb = (int)inst.bridges.size();
        fresh_ok.assign(inst.depots.size(), std::vector<char>((size_t)n_lod * nb, 0));
        cnt0.assign(inst.depots.size(), std::vector<int>(n_lod, 0));
        for (size_t o = 0; o < inst.depots.size(); ++o) {
            int dep = inst.depots[o].id;
            for (int m = 0; m < n_lod; ++m)
                for (int b = 0; b < nb; ++b) {
                    const Bridge& br = inst.bridges[b];
                    int lid = inst.lodgings[m].id;
                    if (!inst.has_arc(lid, br.id) || !inst.has_arc(br.id, dep)) continue;
                    if (inst.get_dist(lid, br.id) + br.w + inst.get_dist(br.id, dep) <= inst.Q + EPS) {
                        fresh_ok[o][(size_t)m * nb + b] = 1;
                        cnt0[o][m]++;
                    }
                }
        }
    }

    double d(int a, int b) const { return inst.get_dist(a, b); }

    void touch(int a, int b) {   // evaporación local de ACS sobre el arco elegido
        if (!local_update) return;
        double& t = tau.at(inst.idx(a), inst.idx(b));
        t = (1.0 - cp.xi) * t + cp.xi * tau.tau0;
    }

    // Regla pseudo-aleatoria (Ec. 10) sobre un conjunto de candidatos:
    //   q <= q0 -> argmax (desempate aleatorio);  si no -> regla proporcional S.
    // Con probabilidad q0 se toma el de mayor puntaje; si no, se sortea entre
    // todos con más probabilidad para los de mayor puntaje. Con q0 = 1 nunca
    // se sortea, que es el caso del greedy.
    // Devuelve -1 si no hay candidatos.
    int pick(const std::vector<Cand>& cands) {
        if (cands.empty()) return -1;
        std::uniform_real_distribution<double> U(0.0, 1.0);
        int chosen = cands.front().bid;
        if (U(rng) <= cp.q0) {
            double best = -1.0; int ties = 0;
            for (const auto& c : cands) {
                if (c.score > best * (1.0 + 1e-12)) {
                    best = c.score; chosen = c.bid; ties = 1;
                } else if (std::fabs(c.score - best) <= 1e-12 * best) {
                    ++ties;
                    if (std::uniform_int_distribution<int>(1, ties)(rng) == 1) chosen = c.bid;
                }
            }
        } else {
            // Ruleta: cada candidato ocupa un tramo proporcional a su puntaje
            double sum = 0.0;
            for (const auto& c : cands) sum += c.score;
            double r = U(rng) * sum, acc = 0.0;
            chosen = cands.back().bid;
            for (const auto& c : cands) {
                acc += c.score;
                if (r <= acc) { chosen = c.bid; break; }
            }
        }
        return chosen;
    }

    // Construye la ruta de un equipo; devuelve la ruta (sin días si no cubrió nada).
    // El equipo sale del depósito y va agregando puentes al día mientras quepan.
    // Cuando no cabe ninguno cierra el día: duerme en un alojamiento, o vuelve
    // al depósito si es el último día o ya no quedan puentes.
    TeamRoute build_route(int team_id, int depot_pos) {
        const int depot = inst.depots[depot_pos].id;
        const int nb = (int)inst.bridges.size();

        TeamRoute route;
        route.team_id  = team_id;
        route.depot_id = depot;                 // Ec. (3)-(4): fijado al iniciar

        // cnt[m]: puentes aún no visitados que m puede servir desde un día fresco.
        // Parte de cnt0, descontando los puentes que otros equipos ya visitaron,
        // y baja cada vez que este equipo visita uno.
        std::vector<int> cnt = cnt0[depot_pos];
        std::vector<char> pending(nb, 0);
        for (int bid : unv) pending[inst.bridge_pos[bid]] = 1;
        for (int b = 0; b < nb; ++b) {
            if (pending[b]) continue;
            for (int m = 0; m < n_lod; ++m)
                if (fresh_ok[depot_pos][(size_t)m * nb + b]) cnt[m]--;
        }

        int    pos       = depot;      // dónde está parado ahora
        int    day       = 0;          // día actual (0 = día 1)
        double remaining = inst.Q;     // minutos de turno que le quedan hoy
        DaySegment seg;                // el día que está armando

        while (!unv.empty()) {
            const bool last_day = (day + 1 >= n_days);
            const bool last_unv = (unv.size() == 1);

            // N_i^k: puentes factibles desde pos con el turno restante  // PAUTA restricciones: codificación restringida, no hace falta reparar
            // Un puente es candidato si hay arco desde pos, cabe en lo que queda del
            // turno y después todavía alcanza el tiempo para cerrar el día. Los demás
            // no se consideran, así la solución sale factible sin repararla.

            std::vector<Cand> cands;
            for (int bid : unv) {
                if (!inst.has_arc(pos, bid)) continue;
                const Bridge& br = *inst.find_bridge(bid);
                double dd    = d(pos, bid);
                double after = remaining - dd - br.w;   // turno que sobra tras visitarlo
                if (after < -EPS) continue;             // no cabe en el turno

                // Reserva para terminar el día: depósito o, si no es el último
                // día ni el último puente, un alojamiento viable. `need` son los
                // minutos que hacen falta desde ese puente para llegar al más
                // cercano de ellos. Garantiza que no quede varada (Ec. 9).
                double need = inst.has_arc(bid, depot) ? d(bid, depot)
                                                       : std::numeric_limits<double>::max();
                if (!last_day && !last_unv) {
                    int bpos = inst.bridge_pos[bid];
                    for (int m = 0; m < n_lod; ++m) {
                        int lid = inst.lodgings[m].id;
                        if (!inst.has_arc(bid, lid)) continue;
                        int c = cnt[m] - (fresh_ok[depot_pos][(size_t)m * nb + bpos] ? 1 : 0);
                        if (c > 0) need = std::min(need, d(bid, lid));
                    }
                }
                if (need > after + EPS) continue;       // no sobra tiempo para cerrar el día

                // Puntaje: feromona del arco y cercanía (eta = 1/distancia)
                double eta = (dd > 1e-9) ? 1.0 / dd : 1e9;
                double tv  = tau.at(inst.idx(pos), inst.idx(bid));
                cands.push_back({bid, std::pow(tv, cp.alpha) * std::pow(eta, cp.beta)});
            }

            if (!cands.empty()) {
                int chosen = pick(cands);          // regla pseudo-aleatoria (Ec. 10)
                const Bridge& br = *inst.find_bridge(chosen);
                touch(pos, chosen);
                remaining -= d(pos, chosen) + br.w;        // viaje + inspección
                seg.bridges_visited.push_back(chosen);
                pos = chosen;
                unv.erase(std::find(unv.begin(), unv.end(), chosen));
                int bpos = inst.bridge_pos[chosen];
                pending[bpos] = 0;
                for (int m = 0; m < n_lod; ++m)
                    if (fresh_ok[depot_pos][(size_t)m * nb + bpos]) cnt[m]--;
                continue;
            }

            // N_i^k vacío: cerrar el día
            // Pernoctar (Ec. 6-7) en el alojamiento viable más cercano que quepa,
            // si queda algún día; si no, el equipo vuelve a su depósito (Ec. 8).
            // El alojamiento también se elige con la Ec. (10): los alojamientos
            // son componentes del grafo de construcción G_C, con feromona tau_im
            // y eta_im = 1/d_im (ACS aprende cuáles conviene usar).
            int best_lodge = -1;
            if (!last_day && !seg.bridges_visited.empty()) {
                std::vector<Cand> lodges;
                for (int m = 0; m < n_lod; ++m) {
                    int lid = inst.lodgings[m].id;
                    if (cnt[m] <= 0 || !inst.has_arc(pos, lid)) continue;
                    double dl = d(pos, lid);
                    if (dl > remaining + EPS) continue;
                    // Costo inmediato de elegir este alojamiento: el viaje hasta él
                    // más su tarifa por noche (ambos suman en la Ec. 1)
                    double cost_m = dl + inst.lodgings[m].rate;
                    double eta = (cost_m > 1e-9) ? 1.0 / cost_m : 1e9;
                    double tv  = tau.at(inst.idx(pos), inst.idx(lid));
                    lodges.push_back({lid, std::pow(tv, cp.alpha) * std::pow(eta, cp.beta)});
                }
                best_lodge = pick(lodges);
            }
            // Sin alojamiento posible (último día o no quedan puentes): la ruta
            // termina y el equipo vuelve al depósito, lo que se hace más abajo.
            if (best_lodge == -1) break;

            seg.lodging_end = best_lodge;
            touch(pos, best_lodge);
            route.days.push_back(seg);
            seg = DaySegment();
            pos = best_lodge;                // Ec. (7): el día k+1 parte de m
            remaining = inst.Q;
            ++day;
        }

        // Último día: regreso al depósito de origen (Ec. 8)
        if (!seg.bridges_visited.empty()) {
            seg.lodging_end = -1;
            touch(pos, depot);
            route.days.push_back(seg);
        }
        return route;
    }
};

}  // namespace

// Una hormiga construye una solución
// completa (un Solution, ver solution.hpp), armando la ruta de un equipo tras
// otro (build_route) hasta que no queden puentes pendientes.
Solution construct_solution(const Instance& inst, const ConstructParams& cp,  // PAUTA construcción de una hormiga
                            Pheromone& tau, bool local_update, std::mt19937& rng) {
    Builder B(inst, cp, tau, local_update, rng);
    Solution sol;

    // Equipos solicitados: posición de depósito de cada uno, en orden
    // (con 2 depósitos y 1 equipo por depósito queda [depósito 0, depósito 1])
    std::vector<int> requested;
    if (cp.total_teams > 0) {
        for (int t = 0; t < cp.total_teams; ++t) requested.push_back(t % (int)inst.depots.size());
    } else {
        for (size_t o = 0; o < inst.depots.size(); ++o)
            for (int t = 0; t < cp.teams_per_depot; ++t) requested.push_back((int)o);
    }

    int team_id = 0;
    size_t next = 0;
    while (!B.unv.empty()) {
        int depot_pos = 0;
        bool extra = (next >= requested.size());
        if (!extra) {
            depot_pos = requested[next++];
        } else {
            // En el modelo E es solo una cota superior: si los equipos pedidos
            // no alcanzan, se despacha otro desde el depósito más cercano a lo pendiente.
            double best = std::numeric_limits<double>::max();
            for (size_t o = 0; o < inst.depots.size(); ++o)
                for (int bid : B.unv) {
                    int dep = inst.depots[o].id;
                    if (inst.has_arc(dep, bid) && inst.get_dist(dep, bid) < best) {
                        best = inst.get_dist(dep, bid);
                        depot_pos = (int)o;
                    }
                }
        }
        TeamRoute r = B.build_route(team_id, depot_pos);
        if (r.days.empty()) {
            if (extra)
                throw std::runtime_error(
                    "Hay puentes inalcanzables dentro del turno Q desde cualquier depósito.");
            continue;               // equipo pedido que no aportó nada: no se despacha
        }
        sol.routes.push_back(r);
        ++team_id;
    }
    return sol;
}

// Inicialización: es la misma construcción pero sin feromona
// (tau = 1 en todo) y sin azar (q0 = 1), o sea, siempre va al más cercano que
// quepa. La solución resultante es factible y sirve de punto de partida del
// ACS, de base para calcular tau0 y de referencia para comparar.
Solution greedy_initialize(const Instance& inst, const InitParams& params) {  // PAUTA inicialización: construcción con tau uniforme y q0 = 1
    std::mt19937 rng(params.seed);
    ConstructParams cp;
    cp.beta            = params.beta;
    cp.q0              = 1.0;                 // siempre argmax
    cp.teams_per_depot = params.teams_per_depot;
    cp.total_teams     = params.total_teams;
    Pheromone tau(inst.n_nodes, 1.0);         // tau uniforme
    return construct_solution(inst, cp, tau, /*local_update=*/false, rng);
}
