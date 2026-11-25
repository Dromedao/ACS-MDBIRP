#pragma once
#include "instance.hpp"
#include "solution.hpp"

#include <random>
#include <vector>

// Construcción de soluciones: Ec. (10) del modelo
//
//   j = argmax_{u in N_i^k} { tau_iu * [eta_iu]^beta }   si q <= q0
//       S (regla aleatoria-proporcional)                  en otro caso
//
//   eta_iu = 1 / d_iu.   N_i^k = puentes aún no visitados que caben en el
//   turno restante del día k (incluyendo una reserva para poder terminar el
//   día en un alojamiento viable o en el depósito). Es la codificación
//   restringida: la hormiga solo puede elegir en N_i^k, por lo que toda
//   solución construida satisface las Ec. (2)-(9) sin reparación.
//   La misma regla elige tanto el siguiente puente como el alojamiento donde
//   se pernocta (ambos son componentes del grafo de construcción G_C; las
//   feromonas tau viven sobre los arcos puente-puente y puente-alojamiento).
//
//   Una hormiga construye una solución completa (construct_solution): arma la
//   ruta de un equipo tras otro, día a día (Builder::build_route), sobre el mismo
//   conjunto de puentes pendientes (Ec. 2), hasta cubrirlos todos.
//
// Inicialización: reducción de la Ec. (10) con tau uniforme y
// q0 = 1 (siempre argmax) => greedy puro guiado por eta. El desempate entre
// candidatos con igual valor usa la semilla.

struct InitParams {
    unsigned int seed            = 42;
    int          teams_per_depot = 1;    // equipos por depósito (default)
    int          total_teams     = 0;    // si > 0, sobreescribe teams_per_depot
    double       beta            = 2.0;  // exponente de eta en Ec. (10)
};

// Parámetros de la regla de transición (Ec. 10) y de la actualización local
struct ConstructParams {
    double alpha = 1.0;    // exponente de tau
    double beta  = 2.0;    // exponente de eta
    double q0    = 1.0;    // prob. de explotación (argmax)
    double xi    = 0.1;    // evaporación local: tau <- (1-xi) tau + xi tau0
    int    teams_per_depot = 1;
    int    total_teams     = 0;
};

// Matriz de feromona tau_ij sobre los nodos de la instancia (índices densos)
struct Pheromone {
    int n = 0;
    double tau0 = 1.0;
    std::vector<double> t;

    Pheromone() = default;
    Pheromone(int n_nodes, double tau0_) : n(n_nodes), tau0(tau0_), t((size_t)n_nodes * n_nodes, tau0_) {}
    double& at(int i, int j)       { return t[(size_t)i * n + j]; }
    double  at(int i, int j) const { return t[(size_t)i * n + j]; }
};

// Una hormiga construye una solución completa (equipo por equipo, día a día).
// Si `local_update` es true aplica la evaporación local de ACS sobre `tau`.
// Lanza std::runtime_error si algún puente es inalcanzable para cualquier equipo.
Solution construct_solution(const Instance& inst, const ConstructParams& cp,
                            Pheromone& tau, bool local_update, std::mt19937& rng);

// Inicialización greedy = construct_solution con tau uniforme y q0 = 1.
Solution greedy_initialize(const Instance& inst, const InitParams& params);
