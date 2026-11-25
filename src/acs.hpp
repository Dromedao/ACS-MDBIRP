#pragma once
#include "initializer.hpp"
#include "localsearch.hpp"

#include <string>

// Ant Colony System sobre el mismo mecanismo de construcción de la inicialización.
//
//   - Cada iteración, n_ants hormigas construyen una solución con la Ec. (10)
//     (q0 < 1 => mezcla explotación / exploración) y evaporación local.
//   - A la mejor hormiga de la iteración se le aplica búsqueda local
//     (2-opt, reubicación de puentes y eliminación de equipo, según --ls-moves)
//     con probabilidad ls_prob.
//   - Criterio de aceptación: la mejor solución global solo se reemplaza por
//     una estrictamente más barata.
//   - Actualización global solo con la mejor solución encontrada:
//         tau_ij <- (1-rho) tau_ij + rho / C_best      para (i,j) en best
//   - tau0 = 1 / (|N| * C_greedy), con C_greedy el costo de la inicialización.
//   - Término: n_iterations iteraciones o time_limit segundos, lo que ocurra antes.

// Parámetros del algoritmo: estos son los valores de fábrica y se pueden
// cambiar desde la línea de comandos (ver el parseo de argv en main.cpp).
// Valores por defecto: los que dieron mejor resultado al probar distintas configuraciones.
// main.cpp los usa tal cual; si se cambian aquí, hay que cambiar también el texto de --help.
namespace defaults {  // PAUTA parámetros y valores por defecto
constexpr unsigned int seed = 42;
constexpr double alpha = 1.55, beta = 3.59, rho = 0.5, xi = 0.26, q0 = 0.64;
constexpr int    ants = 40, iterations = 25;      // ants * iterations = 1000 construcciones
constexpr double ls_prob = 0.25;
}

struct AcsParams {
    unsigned int seed        = defaults::seed;
    int          n_ants      = defaults::ants;
    int          n_iterations = defaults::iterations;
    double       rho         = defaults::rho;
    double       time_limit  = 0.0;        // segundos; 0 = sin límite de tiempo
    double       ls_prob     = defaults::ls_prob;  // prob. de aplicar búsqueda local en una iteración
    LsParams     ls;                       // vecindarios y rondas de la búsqueda local (por defecto, solo relocate)
    std::string  trace_file;               // si no está vacío, guarda la convergencia en CSV
    ConstructParams construct;     // alpha, beta, q0, xi, equipos
};

struct AcsStats {
    int    iterations = 0;     // iteraciones realmente ejecutadas
    double seconds    = 0.0;
};

// Parte de `initial` (solución greedy ya evaluada) y devuelve la mejor encontrada.
Solution run_acs(const Instance& inst, const AcsParams& params, const Solution& initial,
                 AcsStats* stats = nullptr);
