// Óptimo exacto del MDBIRP para instancias pequeñas (hasta 16 puentes) por programación dinámica.
// Sirve para medir la brecha real del ACS. Usa el mismo modelo que el verificador del programa:
// días no vacíos, a lo más |K| días por equipo, turno (viaje + inspección) <= Q, cada día termina en un
// alojamiento salvo el último, que vuelve al depósito de origen; sin límite de equipos (cada uno cuesta v).
//
//   T[s][S][b]    = menor viaje desde el nodo s visitando exactamente S y terminando en el puente b.
//   dayt[s][S][e] = menor viaje de un día que parte de s, cubre S y termina en e (si cabe en Q).
//   H[o][S][m]    = menor costo de un equipo del depósito o tras k días, cubriendo S y durmiendo en m.
//   g[S] = costo de un equipo que cubre exactamente S;  F[S] = mejor partición de S en equipos.
//
// Compilar:  g++ -std=c++17 -O2 -Isrc -o exact experiments/exact.cpp src/instance.cpp
// Uso:       ./exact instances/Instance_b10_a4_d3_s129.dat
#include "instance.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static const double INF = 1e18;

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "uso: %s instancia.dat\n", argv[0]); return 1; }
    Instance inst = parse_instance(argv[1]);
    const int n = (int)inst.bridges.size(), nd = (int)inst.depots.size(), nm = (int)inst.lodgings.size();
    if (n > 16) { std::fprintf(stderr, "demasiados puentes (%d)\n", n); return 1; }
    const int K = (int)inst.days.size();
    const int FULL = (1 << n) - 1;
    const double Q = inst.Q + 1e-6;

    // nodos de partida: depósitos 0..nd-1, alojamientos nd..nd+nm-1
    std::vector<int> start_id, bid(n), mid(nm);
    for (auto& d : inst.depots) start_id.push_back(d.id);
    for (auto& l : inst.lodgings) start_id.push_back(l.id);
    for (int i = 0; i < n; ++i) bid[i] = inst.bridges[i].id;
    const int ns = nd + nm;

    auto dist = [&](int a, int b) { return inst.has_arc(a, b) ? inst.get_dist(a, b) : INF; };
    std::vector<double> W(FULL + 1, 0.0);
    for (int S = 1; S <= FULL; ++S) { int b = __builtin_ctz(S); W[S] = W[S & (S - 1)] + inst.bridges[b].w; }

    // Caminos mínimos por subconjunto y viajes de un día
    std::vector<std::vector<double>> dayt(ns, std::vector<double>(((size_t)FULL + 1) * ns, INF));  // [s][S*ns+e]
    std::vector<double> T(((size_t)FULL + 1) * n);
    for (int s = 0; s < ns; ++s) {
        std::fill(T.begin(), T.end(), INF);
        for (int b = 0; b < n; ++b) T[((size_t)1 << b) * n + b] = dist(start_id[s], bid[b]);
        for (int S = 1; S <= FULL; ++S)
            for (int b = 0; b < n; ++b) {
                double c = T[(size_t)S * n + b];
                if (c >= INF || !(S >> b & 1)) continue;
                for (int j = 0; j < n; ++j) {
                    if (S >> j & 1) continue;
                    double dd = dist(bid[b], bid[j]);
                    if (dd >= INF) continue;
                    double& t = T[(size_t)(S | 1 << j) * n + j];
                    if (c + dd < t) t = c + dd;
                }
            }
        for (int S = 1; S <= FULL; ++S)
            for (int e = 0; e < ns; ++e) {
                double best = INF;
                for (int b = 0; b < n; ++b) {
                    double c = T[(size_t)S * n + b];
                    if (c >= INF) continue;
                    double dd = dist(bid[b], start_id[e]);
                    if (dd < INF) best = std::min(best, c + dd);
                }
                if (best + W[S] <= Q) dayt[s][(size_t)S * ns + e] = best;
            }
    }

    // Un equipo: g[S]
    std::vector<double> g(FULL + 1, INF);
    for (int o = 0; o < nd; ++o) {
        // H[S*nm + m] después de k días
        std::vector<double> H(((size_t)FULL + 1) * nm, INF), H2;
        for (int S = 1; S <= FULL; ++S) {
            g[S] = std::min(g[S], inst.v + dayt[o][(size_t)S * ns + o]);            // 1 día, vuelve al depósito
            for (int m = 0; m < nm; ++m) {
                double t = dayt[o][(size_t)S * ns + nd + m];
                if (t < INF) H[(size_t)S * nm + m] = t + inst.lodgings[m].rate;
            }
        }
        for (int day = 2; day <= K; ++day) {
            H2.assign(((size_t)FULL + 1) * nm, INF);
            for (int S1 = 1; S1 <= FULL; ++S1) {
                const int rest = FULL & ~S1;
                for (int m = 0; m < nm; ++m) {
                    double c = H[(size_t)S1 * nm + m];
                    if (c >= INF) continue;
                    for (int S2 = rest; S2; S2 = (S2 - 1) & rest) {
                        const double* row = &dayt[nd + m][(size_t)S2 * ns];
                        const int S = S1 | S2;
                        if (row[o] < INF) g[S] = std::min(g[S], inst.v + c + row[o]);
                        for (int m2 = 0; m2 < nm; ++m2)
                            if (row[nd + m2] < INF) {
                                double& h = H2[(size_t)S * nm + m2];
                                double val = c + row[nd + m2] + inst.lodgings[m2].rate;
                                if (val < h) h = val;
                            }
                    }
                }
            }
            H.swap(H2);
        }
    }

    // partición en equipos
    std::vector<double> F(FULL + 1, INF);
    F[0] = 0;
    for (int S = 1; S <= FULL; ++S) {
        const int low = S & -S, rest = S ^ low;
        for (int sub = rest;; sub = (sub - 1) & rest) {
            const int Tm = sub | low;
            if (g[Tm] < INF && F[S ^ Tm] < INF) F[S] = std::min(F[S], g[Tm] + F[S ^ Tm]);
            if (sub == 0) break;
        }
    }
    const double opt = F[FULL];
    std::printf("%s,%.0f,%d\n", argv[1], opt, (int)std::lround(opt / inst.v));
    return opt < INF ? 0 : 2;
}
