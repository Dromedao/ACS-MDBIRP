// Punto de entrada: lee la instancia y los parámetros de la línea de comandos,
// arma la solución inicial, corre el ACS, valida y muestra el resultado.
#include "instance.hpp"
#include "solution.hpp"
#include "constraints.hpp"
#include "initializer.hpp"
#include "acs.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Uso
static void print_usage(const char* prog) {  // PAUTA README/parámetros: --help con los defaults
    std::cerr <<
        "Uso: " << prog << " <instancia.dat> [opciones]\n"
        "\n"
        "  <instancia.dat>          Ruta al archivo de instancia (AMPL/GLPK)\n"
        "\n"
        "Parámetros de ACS (ninguno está fijo en el código):\n"
        "  --seed        N          Semilla aleatoria              (default: 42)\n"
        "  --alpha       F          Exponente de tau en Ec.10      (default: 1.55)\n"
        "  --beta        F          Exponente de eta en Ec.10      (default: 3.59)\n"
        "  --rho         F          Evaporación global             (default: 0.5)\n"
        "  --xi          F          Evaporación local              (default: 0.26)\n"
        "  --q0          F          Parámetro q0 de Ec.10 en [0,1] (default: 0.64)\n"
        "  --ants        N          Hormigas por iteración         (default: 40)\n"
        "  (el greedy inicial usa solo --beta: tau es uniforme y q0 = 1, así que alpha, q0 y xi no lo afectan)\n"
        "\n"
        "Criterio de término (lo que ocurra primero):\n"
        "  --iterations  N          Iteraciones ACS; 0 = solo la inicialización greedy (default: 25)\n"
        "  --time        S          Límite de tiempo del ACS en segundos; 0 = sin límite (default: 0)\n"
        "\n"
        "Búsqueda local (se aplica a la mejor hormiga de cada iteración):\n"
        "  --ls-moves    M          none | 2opt | relocate | eliminate | all   (default: relocate)\n"
        "  --ls-prob     F          Probabilidad de aplicarla en una iteración, en [0,1] (default: 0.25)\n"
        "  --ls-iters    N          Rondas de mejora como máximo por aplicación (default: 30)\n"
        "\n"
        "Equipos (cota inferior; si no alcanzan se despachan más, E es solo una cota):\n"
        "  --teams-per-depot  N     Equipos por depósito           (default: 1)\n"
        "  --total-teams      N     Total de equipos (sobreescribe --teams-per-depot)\n"
        "\n"
        "Salida:\n"
        "  --csv                    Imprime solo una línea: instancia,semilla,costo_greedy,costo_final,\n"
        "                           equipos,iteraciones,segundos_acs,valida,equipos_greedy,viaje,\n"
        "                           despacho,alojamiento,noches,cota_equipos\n"
        "  --trace       archivo    Guarda la convergencia (costo por iteración) en un CSV\n"
        "  --solution    archivo    Guarda la solución final (una fila por equipo y día) en un CSV\n"
        "\n"
        "Ejemplo:\n"
        "  " << prog << " instances/Instance_b10_a4_d3_s129.dat --seed 42 --beta 2.0 --total-teams 4\n";
}

[[noreturn]] static void die(const std::string& msg) {
    std::cerr << "Error: " << msg << "\n";
    std::exit(1);
}

// Cota inferior del número de equipos. Con T equipos hay que cubrir el trabajo de
// inspección más el viaje mínimo, y eso debe caber en T * |K| * Q minutos:
//     suma(w) + viaje_min(T) <= T * |K| * Q
// El viaje mínimo entre puentes es un bosque de T componentes sobre los puentes:
// el árbol generador mínimo (con min(d_ij, d_ji) por arista) sin sus T-1 aristas
// más pesadas; además cada equipo sale y vuelve a un depósito, al menos con el
// arco de salida y de regreso más cortos. No considera el desperdicio al
// empaquetar días, por eso es una cota y no necesariamente el óptimo. Se asume
// que pasar por un alojamiento no acorta el viaje entre dos puentes.
static int team_lower_bound(const Instance& inst) {
    const int n = (int)inst.bridges.size();
    double work = 0.0;
    for (const auto& b : inst.bridges) work += b.w;
    const double cap = inst.days.size() * inst.Q;

    // Prim sobre los puentes
    std::vector<double> key(n, 1e300), edges;
    std::vector<char> in(n, 0);
    key[0] = 0.0;
    bool connected = true;
    for (int it = 0; it < n; ++it) {
        int u = -1;
        for (int i = 0; i < n; ++i) if (!in[i] && (u < 0 || key[i] < key[u])) u = i;
        if (key[u] > 1e299) { connected = false; break; }
        in[u] = 1;
        if (it > 0) edges.push_back(key[u]);
        for (int v = 0; v < n; ++v) {
            if (in[v]) continue;
            int a = inst.bridges[u].id, b = inst.bridges[v].id;
            bool x = inst.has_arc(a, b), y = inst.has_arc(b, a);
            if (!x && !y) continue;
            double w = x && y ? std::min(inst.get_dist(a, b), inst.get_dist(b, a))
                              : (x ? inst.get_dist(a, b) : inst.get_dist(b, a));
            if (w < key[v]) key[v] = w;
        }
    }
    if (!connected) edges.clear();
    std::sort(edges.begin(), edges.end(), std::greater<double>());
    double mst = 0.0;
    for (double e : edges) mst += e;

    double out = 1e300, back = 1e300;
    for (const auto& o : inst.depots)
        for (const auto& b : inst.bridges) {
            if (inst.has_arc(o.id, b.id)) out = std::min(out, inst.get_dist(o.id, b.id));
            if (inst.has_arc(b.id, o.id)) back = std::min(back, inst.get_dist(b.id, o.id));
        }
    if (out > 1e299) out = 0.0;
    if (back > 1e299) back = 0.0;

    double forest = mst;
    for (int t = 1; t <= n; ++t) {
        if (t > 1 && (size_t)(t - 2) < edges.size()) forest -= edges[t - 2];
        if (work + forest + t * (out + back) <= t * cap + 1e-9) return t;
    }
    return n;
}

// Guarda la solución: una fila por día (equipo, depósito, día, alojamiento, puentes).
static void write_solution(const std::string& path, const Solution& sol) {
    std::ofstream f(path);
    if (!f) die("no se pudo abrir " + path);
    f << "team,depot,day,lodging,bridges\n";
    for (const auto& r : sol.routes)
        for (size_t k = 0; k < r.days.size(); ++k) {
            f << r.team_id << "," << r.depot_id << "," << (k + 1) << "," << r.days[k].lodging_end << ",";
            for (size_t i = 0; i < r.days[k].bridges_visited.size(); ++i)
                f << (i ? " " : "") << r.days[k].bridges_visited[i];
            f << "\n";
        }
}

// main
int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    // La instancia viene por argv, no hay ninguna fija en el código.

    std::string instance_file = argv[1];  // PAUTA instancia por línea de ejecución (argv[1])
    if (instance_file == "--help" || instance_file == "-h") { print_usage(argv[0]); return 0; }
    if (instance_file[0] == '-') {
        std::cerr << "Error: el primer argumento debe ser el archivo de instancia.\n";
        print_usage(argv[0]);
        return 1;
    }

    // Valores por defecto (todos sobreescribibles por argv)
    AcsParams acs;
    acs.construct.alpha = defaults::alpha; acs.construct.beta = defaults::beta;
    acs.construct.q0 = defaults::q0;       acs.construct.xi = defaults::xi;
    acs.construct.teams_per_depot = 1; acs.construct.total_teams = 0;
    bool csv = false;
    std::string solution_file;

    // Lectura de los parámetros. Cada flag llena un campo de AcsParams:
    // alpha, beta, q0 y xi son de la regla de la hormiga; rho y ants del ACS;
    // ls-moves, ls-prob y ls-iters de los operadores (cuáles, con qué probabilidad
    // y cuántas rondas); iterations y time del criterio de término.
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) die("falta valor para " + arg);
            return argv[++i];
        };
        try {
            if      (arg == "--seed")            acs.seed = (unsigned)std::stoul(next());
            else if (arg == "--alpha")           acs.construct.alpha = std::stod(next());  // PAUTA parámetros por línea de ejecución
            else if (arg == "--beta")            acs.construct.beta = std::stod(next());
            else if (arg == "--rho")             acs.rho = std::stod(next());
            else if (arg == "--xi")              acs.construct.xi = std::stod(next());
            else if (arg == "--q0")              acs.construct.q0 = std::stod(next());
            else if (arg == "--ants")            acs.n_ants = std::stoi(next());
            else if (arg == "--iterations")      acs.n_iterations = std::stoi(next());  // PAUTA criterio de término: iteraciones
            else if (arg == "--time")            acs.time_limit = std::stod(next());  // PAUTA criterio de término: tiempo
            else if (arg == "--ls-prob")         acs.ls_prob = std::stod(next());  // PAUTA operadores parametrizados (--ls-prob, --ls-moves, --ls-iters)
            else if (arg == "--ls-iters")        acs.ls.max_rounds = std::stoi(next());
            else if (arg == "--ls-moves") {
                std::string m = next();
                if      (m == "none")     acs.ls.moves = LS_NONE;
                else if (m == "2opt")     acs.ls.moves = LS_2OPT;
                else if (m == "relocate") acs.ls.moves = LS_RELOCATE;
                else if (m == "eliminate") acs.ls.moves = LS_ELIMINATE;
                else if (m == "all")      acs.ls.moves = LS_ALL;
                else die("--ls-moves debe ser none, 2opt, relocate, eliminate o all");
            }
            else if (arg == "--teams-per-depot") acs.construct.teams_per_depot = std::stoi(next());
            else if (arg == "--total-teams")     acs.construct.total_teams = std::stoi(next());
            else if (arg == "--trace")           acs.trace_file = next();
            else if (arg == "--solution")        solution_file = next();
            else if (arg == "--csv")             csv = true;
            else if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
            else { std::cerr << "Opción desconocida: " << arg << "\n"; print_usage(argv[0]); return 1; }
        } catch (const std::exception&) {
            die("valor inválido para " + arg);
        }
    }
    if (acs.construct.q0 < 0 || acs.construct.q0 > 1) die("--q0 debe estar en [0,1]");
    if (acs.rho < 0 || acs.rho > 1 || acs.construct.xi < 0 || acs.construct.xi > 1)
        die("--rho y --xi deben estar en [0,1]");
    if (acs.n_ants < 1 || acs.n_iterations < 0) die("--ants >= 1 y --iterations >= 0");
    if (acs.time_limit < 0) die("--time debe ser >= 0");
    if (acs.ls_prob < 0 || acs.ls_prob > 1) die("--ls-prob debe estar en [0,1]");
    if (acs.ls.max_rounds < 0) die("--ls-iters debe ser >= 0");
    if (acs.construct.teams_per_depot < 0 || acs.construct.total_teams < 0)
        die("número de equipos inválido");

    // Con --csv se silencia la salida normal y al final se imprime una sola línea.
    std::streambuf* real_out = std::cout.rdbuf();
    if (csv) std::cout.rdbuf(nullptr);

    // Lectura de la instancia.
    // parse_instance() carga depósitos, puentes, alojamientos, días, Q, v,
    // tarifas y las matrices de distancia (ver instance.cpp).
    Instance inst;
    try {
        inst = parse_instance(instance_file);  // PAUTA lectura de la instancia
    } catch (const std::exception& e) {
        std::cerr << "Error al parsear instancia: " << e.what() << "\n";
        return 1;
    }
    std::cout << "Instancia: " << instance_file << "\n"
              << "  " << inst.bridges.size() << " puentes, " << inst.depots.size() << " depósitos, "
              << inst.lodgings.size() << " alojamientos, " << inst.days.size()
              << " días, turno Q = " << inst.Q << " min\n";
    std::cout << "Parámetros: semilla " << acs.seed << ", alpha " << acs.construct.alpha
              << ", beta " << acs.construct.beta << ", rho " << acs.rho << ", xi " << acs.construct.xi
              << ", q0 " << acs.construct.q0 << ", " << acs.n_ants << " hormigas, "
              << acs.n_iterations << " iteraciones, ";
    if (acs.construct.total_teams > 0)
        std::cout << acs.construct.total_teams << " equipos en total\n";
    else
        std::cout << acs.construct.teams_per_depot << " equipo(s) por depósito\n";
    if (acs.time_limit > 0) std::cout << "Límite de tiempo: " << acs.time_limit << " s\n";
    std::cout << "Búsqueda local: ";
    if (acs.ls.moves == LS_NONE) std::cout << "desactivada\n\n";
    else std::cout << (acs.ls.moves == LS_ALL ? "2-opt + reubicación + eliminación de equipo" :
                       acs.ls.moves == LS_2OPT ? "2-opt" :
                       acs.ls.moves == LS_RELOCATE ? "reubicación" : "eliminación de equipo")
                   << ", prob " << acs.ls_prob << ", hasta " << acs.ls.max_rounds << " rondas\n\n";

    // Inicialización.
    // Solución inicial greedy: la misma construcción de las hormigas (Ec. 10)
    // con feromona uniforme y q0 = 1, es decir, siempre el candidato más
    // cercano que cabe en el turno. Ver greedy_initialize() en initializer.cpp.
    InitParams ip;
    ip.seed = acs.seed;
    ip.teams_per_depot = acs.construct.teams_per_depot;
    ip.total_teams = acs.construct.total_teams;
    ip.beta = acs.construct.beta;

    Solution sol;
    try {
        sol = greedy_initialize(inst, ip);  // PAUTA inicialización
        evaluate(sol, inst);                                   // Ec. (1)
    } catch (const std::exception& e) {
        std::cerr << "Error en la construcción: " << e.what() << "\n";
        return 1;
    }
    std::cout << std::fixed << std::setprecision(0);
    std::cout << "Solución inicial (greedy, Ec.10 con q0=1): costo " << sol.total_cost
              << ", " << sol.routes.size() << " equipo(s)\n";

    Solution final_sol = sol;
    bool valid = true;
    {   // validación silenciosa de la inicial; la salida completa es la de la solución final
        std::streambuf* old = std::cout.rdbuf(nullptr);
        valid = validate_solution(sol, inst);                   // Ec. (2)-(9)
        std::cout.rdbuf(old);
        std::cout << std::fixed << std::setprecision(0);
    }

    // Algoritmo principal: ACS sobre la misma construcción
    // run_acs() está en acs.cpp: ahí están el criterio de aceptación y el de término.
    // Solo corre si se pidieron iteraciones y la solución inicial es válida.
    AcsStats stats;
    if (acs.n_iterations > 0 && valid) {
        Solution best;
        try {
            best = run_acs(inst, acs, sol, &stats);
        } catch (const std::exception& e) {
            std::cerr << "Error en ACS: " << e.what() << "\n";
            return 1;
        }
        evaluate(best, inst);
        std::cout << "Solución ACS: costo " << best.total_cost << " ("
                  << std::setprecision(1) << 100.0 * (sol.total_cost - best.total_cost) / sol.total_cost
                  << std::setprecision(0) << "% menos que la inicial), "
                  << stats.iterations << " iteraciones, "
                  << std::setprecision(2) << stats.seconds << " s\n" << std::setprecision(0);
        final_sol = best;
    }

    std::cout << "\nSolución final:\n";
    print_solution(final_sol, inst);
    valid = validate_solution(final_sol, inst) && valid;
    if (!solution_file.empty()) write_solution(solution_file, final_sol);

    if (csv) {
        int nights = 0;
        for (const auto& r : final_sol.routes)
            for (const auto& d : r.days) if (d.lodging_end != -1) ++nights;
        int lb_teams = team_lower_bound(inst);

        std::ostream out(real_out);
        out << std::fixed << std::setprecision(2)
            << instance_file << "," << acs.seed << "," << sol.total_cost << ","
            << final_sol.total_cost << "," << final_sol.routes.size() << ","
            << stats.iterations << "," << stats.seconds << "," << (valid ? 1 : 0) << ","
            << sol.routes.size() << "," << final_sol.travel_cost << ","
            << final_sol.dispatch_cost << "," << final_sol.lodging_cost << ","
            << nights << "," << lb_teams << "\n";
    }
    return valid ? 0 : 1;  // PAUTA factibilidad: el programa sale con 0 solo si la solución es válida
}
