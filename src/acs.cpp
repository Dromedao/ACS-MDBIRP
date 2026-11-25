#include "acs.hpp"

#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>

// Ciclo del Ant Colony System. En cada iteración las hormigas construyen
// soluciones, se compara su costo y los arcos de la mejor solución reciben
// más feromona. La hormiga no sabe qué tan buena es su solución; lo aprendido
// les llega a las demás solo a través de la matriz de feromona.
Solution run_acs(const Instance& inst, const AcsParams& params, const Solution& initial,
                 AcsStats* stats) {
    std::mt19937 rng(params.seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    const auto t0 = std::chrono::steady_clock::now();
    auto elapsed = [&]() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };

    // La mejor solución parte siendo la del greedy, así que el resultado
    // nunca es peor que ella.
    Solution best = initial;

    // Feromona inicial igual en todos los arcos. El costo del greedy solo se
    // usa para dejar tau0 en la escala del problema.
    const double n_bridges = static_cast<double>(inst.bridges.size());
    Pheromone tau(inst.n_nodes, 1.0 / (n_bridges * initial.total_cost));

    // Traza de convergencia: una fila por iteración (la fila 0 es la inicial).
    std::ofstream trace;
    if (!params.trace_file.empty()) {
        trace.open(params.trace_file);
        if (!trace) throw std::runtime_error("no se pudo abrir el archivo de traza: " + params.trace_file);
        trace << "iter,best_cost,iter_best_cost,seconds\n";
        trace << 0 << "," << best.total_cost << "," << best.total_cost << ",0\n";
    }

    // Cada iteración: las hormigas construyen, se aplica búsqueda local a la mejor,
    // se decide si reemplaza a la mejor global, se actualiza la feromona y se revisa el término.
    int done = 0;
    for (int it = 1; it <= params.n_iterations; ++it) {  // PAUTA término por iteraciones
        Solution iter_best;          // mejor solución de esta iteración
        bool have_iter = false;

        // Cada hormiga arma una solución completa y se calcula su costo.
        // Con local_update, cada arco recorrido pierde un poco de feromona
        // para que las demás hormigas prueben otros caminos.
        for (int a = 0; a < params.n_ants; ++a) {
            // Una hormiga puede quedar en un callejón sin salida (deja un puente que
            // ningún equipo nuevo alcanza dentro de Q): se descarta y se sigue.
            Solution s;
            try {
                s = construct_solution(inst, params.construct, tau, /*local_update=*/true, rng);
            } catch (const std::runtime_error&) {
                continue;
            }
            evaluate(s, inst);
            if (!have_iter || s.total_cost < iter_best.total_cost) { iter_best = s; have_iter = true; }
        }

        // Operadores: búsqueda local (localsearch.cpp) sobre la mejor hormiga de
        // la iteración. --ls-prob decide con qué probabilidad se aplica y
        // --ls-moves / --ls-iters cuáles operadores y cuántas rondas.
        if (have_iter && params.ls.moves != LS_NONE && (params.ls_prob >= 1.0 || U(rng) < params.ls_prob))
            local_search(iter_best, inst, params.ls);

        // Solo se reemplaza la mejor global si la candidata es estrictamente más
        // barata (el 1e-9 evita cambios por redondeo).
        // Nunca se acepta una solución peor.
        if (have_iter && iter_best.total_cost < best.total_cost - 1e-9) {  // PAUTA criterio de aceptación
            best = iter_best;
        }

        // Actualización global, solo con la mejor solución. Cada arco (i,j) que
        // usa queda con  tau_ij = (1 - rho) * tau_ij + rho / costo_mejor,
        // así las hormigas siguientes tienden a repetir esos tramos.
        const double deposit = params.rho / best.total_cost;
        for (const auto& route : best.routes)
            for (size_t k = 0; k < route.days.size(); ++k)
                for_each_arc(route, k, day_start_node(route, k), [&](int i, int j) {
                    double& t = tau.at(inst.idx(i), inst.idx(j));
                    t = (1.0 - params.rho) * t + deposit;
                });

        done = it;
        if (trace)
            trace << it << "," << best.total_cost << ","
                  << (have_iter ? iter_best.total_cost : best.total_cost) << "," << elapsed() << "\n";
        // Criterio de término: el for corta por --iterations y la línea de abajo
        // corta por --time (segundos).
        // Termina lo que ocurra primero.
        if (params.time_limit > 0 && elapsed() >= params.time_limit) break;  // PAUTA término por tiempo
    }
    if (stats) { stats->iterations = done; stats->seconds = elapsed(); }
    return best;
}
