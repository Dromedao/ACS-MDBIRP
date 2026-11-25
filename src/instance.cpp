#include "instance.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <stdexcept>

// Helpers de Instance

void Instance::build_index() {
    int max_id = -1;
    for (const auto& d : depots)   max_id = std::max(max_id, d.id);
    for (const auto& b : bridges)  max_id = std::max(max_id, b.id);
    for (const auto& l : lodgings) max_id = std::max(max_id, l.id);
    for (const auto& kv : dist)    max_id = std::max({max_id, kv.first.first, kv.first.second});

    node_idx.assign(max_id + 1, -1);
    depot_pos.assign(max_id + 1, -1);
    bridge_pos.assign(max_id + 1, -1);
    lodging_pos.assign(max_id + 1, -1);

    n_nodes = 0;
    auto reg = [&](int id, std::vector<int>& pos, size_t p) {
        if (node_idx[id] != -1)
            throw std::runtime_error("Id de nodo duplicado: " + std::to_string(id));
        node_idx[id] = n_nodes++;
        pos[id] = static_cast<int>(p);
    };
    for (size_t i = 0; i < depots.size();   ++i) reg(depots[i].id,   depot_pos,   i);
    for (size_t i = 0; i < bridges.size();  ++i) reg(bridges[i].id,  bridge_pos,  i);
    for (size_t i = 0; i < lodgings.size(); ++i) reg(lodgings[i].id, lodging_pos, i);

    dm.assign(static_cast<size_t>(n_nodes) * n_nodes, -1.0);
    for (const auto& kv : dist) {
        int a = node_idx[kv.first.first], b = node_idx[kv.first.second];
        if (a < 0 || b < 0)
            throw std::runtime_error("La matriz d[] referencia un nodo inexistente: " +
                std::to_string(kv.first.first) + " -> " + std::to_string(kv.first.second));
        dm[static_cast<size_t>(a) * n_nodes + b] = kv.second;
    }
}

bool Instance::has_arc(int from, int to) const {
    if (from < 0 || to < 0 || from >= (int)node_idx.size() || to >= (int)node_idx.size()) return false;
    int a = node_idx[from], b = node_idx[to];
    return a >= 0 && b >= 0 && dm[static_cast<size_t>(a) * n_nodes + b] >= 0.0;
}

double Instance::get_dist(int from, int to) const {
    if (!has_arc(from, to)) {
        throw std::runtime_error(
            "Distancia no encontrada: " + std::to_string(from) +
            " -> " + std::to_string(to));
    }
    return dm[static_cast<size_t>(node_idx[from]) * n_nodes + node_idx[to]];
}

double Instance::get_lodging_rate(int lodging_id) const {
    const Lodging* l = find_lodging(lodging_id);
    if (!l) throw std::runtime_error("Alojamiento no encontrado: " + std::to_string(lodging_id));
    return l->rate;
}

static const auto* lookup(const std::vector<int>& pos, int id) {
    return (id >= 0 && id < (int)pos.size() && pos[id] >= 0) ? &pos[id] : nullptr;
}
const Depot* Instance::find_depot(int id) const {
    auto p = lookup(depot_pos, id);   return p ? &depots[*p] : nullptr;
}
const Bridge* Instance::find_bridge(int id) const {
    auto p = lookup(bridge_pos, id);  return p ? &bridges[*p] : nullptr;
}
const Lodging* Instance::find_lodging(int id) const {
    auto p = lookup(lodging_pos, id); return p ? &lodgings[*p] : nullptr;
}

// Utilidades de parseo

static std::string strip_comment(const std::string& line) {
    auto pos = line.find('#');
    return (pos == std::string::npos) ? line : line.substr(0, pos);
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Verifica si la línea comienza con 'kw' seguido de espacio, tab, ':', ';', '[' o fin
static bool starts_with_kw(const std::string& line, const std::string& kw) {
    if (line.size() < kw.size()) return false;
    if (line.substr(0, kw.size()) != kw) return false;
    if (line.size() == kw.size()) return true;
    char next = line[kw.size()];
    return (next == ' ' || next == '\t' || next == ':' || next == ';' || next == '[');
}

// Extrae valor escalar de "param X:= VAL;" o "param X:= VAL"
static double parse_scalar(const std::string& line) {
    auto pos = line.find(":=");
    if (pos == std::string::npos) throw std::runtime_error("Línea mal formada (falta ':='): " + line);
    std::string val = line.substr(pos + 2);
    auto sc = val.find(';');
    if (sc != std::string::npos) val = val.substr(0, sc);
    val = trim(val);
    if (val.empty()) throw std::runtime_error("Línea sin valor: " + line);
    size_t used = 0;
    double x;
    try { x = std::stod(val, &used); }
    catch (const std::exception&) { throw std::runtime_error("Valor no numérico: " + line); }
    if (used != val.size()) throw std::runtime_error("Valor no numérico: " + line);
    return x;
}

// Extrae lista de enteros de "set X:= id1 id2 ...;"
static std::vector<int> parse_id_list(const std::string& line) {
    std::vector<int> ids;
    auto pos = line.find(":=");
    if (pos == std::string::npos) throw std::runtime_error("Línea mal formada (falta ':='): " + line);
    std::string vals = line.substr(pos + 2);
    auto sc = vals.find(';');
    if (sc != std::string::npos) vals = vals.substr(0, sc);
    std::istringstream ss(vals);
    int v;
    while (ss >> v) ids.push_back(v);
    if (!ss.eof()) throw std::runtime_error("Lista con un valor no entero: " + line);
    return ids;
}

static bool is_in(const std::vector<int>& v, int id) {
    return std::find(v.begin(), v.end(), id) != v.end();
}

// Parser principal

// Lectura de la instancia: lee el archivo .dat (formato
// AMPL/GLPK) línea a línea: los conjuntos O, N, M, K; los escalares D, Q, v; las
// tarifas f; las coordenadas y tiempos de inspección w; y las matrices de
// distancia entre puentes, puentes-alojamientos y depósitos. Al final arma los
// índices densos (build_index) que usa el resto del programa.
Instance parse_instance(const std::string& filepath) {  // PAUTA lectura de la instancia
    std::ifstream file(filepath);
    if (!file.is_open())
        throw std::runtime_error("No se puede abrir: " + filepath);

    Instance inst;
    inst.D = 0; inst.Q = 0.0; inst.v = 0.0;

    std::vector<int> depot_ids, bridge_ids, lodging_ids;

    // Secciones del archivo
    enum class Sec {
        NONE,
        PARAM_F,        // tarifas de alojamiento
        PARAM_COORD,    // xcoord ycoord w q
        DIST_MATRIX     // param d[*,*] y sus sub-bloques
    } sec = Sec::NONE;

    // Para la sección de matrices de distancia
    std::vector<int> col_ids;         // columnas del sub-bloque actual

    std::string raw;
    while (std::getline(file, raw)) {
        std::string line = trim(strip_comment(raw));
        if (line.empty()) continue;

        // Terminador de sección: línea que ES solo ";" o que TERMINA en ";"
        bool is_terminator = (line == ";");
        // Nota: líneas como "set O:= 0 1;" terminan en ";" pero NO son
        // terminadores de sección multi-línea: se tratan con sus propias
        // reglas abajo. Solo reseteamos sección si ya estamos en una.
        if (is_terminator) {
            sec = Sec::NONE;
            col_ids.clear();
            continue;
        }

        // Sets (siempre en una sola línea terminada en ";")
        if (starts_with_kw(line, "set O"))  { depot_ids   = parse_id_list(line); continue; }
        if (starts_with_kw(line, "set N"))  { bridge_ids  = parse_id_list(line); continue; }
        if (starts_with_kw(line, "set M"))  { lodging_ids = parse_id_list(line); continue; }
        if (starts_with_kw(line, "set K"))  { inst.days   = parse_id_list(line); continue; }

        // Escalares (una sola línea terminada en ";")
        if (starts_with_kw(line, "param D")) { inst.D = (int)parse_scalar(line); continue; }
        if (starts_with_kw(line, "param Q")) { inst.Q = parse_scalar(line);       continue; }
        if (starts_with_kw(line, "param v")) { inst.v = parse_scalar(line);       continue; }

        // param f:= (tarifas de alojamiento: multi-línea)
        if (starts_with_kw(line, "param f")) {
            sec = Sec::PARAM_F;
            // Valores en la misma línea (si los hay)
            auto pos = line.find(":=");
            if (pos != std::string::npos) {
                std::string rest = trim(line.substr(pos + 2));
                // Quitar posible ";" al final
                if (!rest.empty() && rest.back() == ';') {
                    rest.pop_back();
                    sec = Sec::NONE;  // sección cerrada en misma línea
                }
                if (!rest.empty()) {
                    std::istringstream ss(rest);
                    int id; double rate;
                    while (ss >> id >> rate)
                        inst.lodgings.push_back({id, 0.0, 0.0, rate});
                }
            }
            continue;
        }
        if (sec == Sec::PARAM_F) {
            // Líneas con "id tarifa"
            std::istringstream ss(line);
            int id; double rate;
            while (ss >> id >> rate)
                inst.lodgings.push_back({id, 0.0, 0.0, rate});
            continue;
        }

        // param: xcoord ycoord w q:= (coordenadas y tiempos de inspección)
        // La línea tiene un TAB entre "param:" y "xcoord"
        if (line.find("param:") != std::string::npos &&
            line.find("xcoord") != std::string::npos) {
            sec = Sec::PARAM_COORD;
            continue;
        }
        if (sec == Sec::PARAM_COORD) {
            std::istringstream ss(line);
            int id; double x, y, w; int q;
            if (!(ss >> id >> x >> y >> w >> q)) continue;

            if (is_in(depot_ids, id)) {
                inst.depots.push_back({id, x, y});
            } else if (is_in(bridge_ids, id)) {
                inst.bridges.push_back({id, x, y, w, q});
            } else if (is_in(lodging_ids, id)) {
                // Completar coordenadas del alojamiento (ya tiene rate de param f)
                bool found = false;
                for (auto& l : inst.lodgings) {
                    if (l.id == id) { l.x = x; l.y = y; found = true; break; }
                }
                if (!found) inst.lodgings.push_back({id, x, y, 0.0});
            }
            continue;
        }

        // param d[*,*]: matrices de distancia (multi-bloque)
        if (starts_with_kw(line, "param d")) {
            sec = Sec::DIST_MATRIX;
            col_ids.clear();
            continue;
        }

        if (sec == Sec::DIST_MATRIX) {
            // Cabecera de sub-bloque: ": id1 id2 ... :="
            // Siempre empieza con ':' y contiene ':='
            if (line[0] == ':' && line.find(":=") != std::string::npos) {
                // Extraer IDs de columnas entre el primer ':' y ':='
                auto p1 = line.find(':') + 1;
                auto p2 = line.find(":=");
                if (p2 > p1) {
                    std::string cols_str = line.substr(p1, p2 - p1);
                    std::istringstream ss(cols_str);
                    col_ids.clear();
                    int c;
                    while (ss >> c) col_ids.push_back(c);
                }
                continue;
            }

            // Fila de datos: "row_id val1 val2 ..."
            if (!col_ids.empty()) {
                std::istringstream ss(line);
                int row_id; double val;
                if (!(ss >> row_id)) continue;
                size_t ci = 0;
                while (ss >> val && ci < col_ids.size()) {
                    inst.dist[{row_id, col_ids[ci]}] = val;
                    ++ci;
                }
            }
            continue;
        }
    }

    // Validación de consistencia entre los sets declarados y los datos leídos
    if (inst.bridges.size() != bridge_ids.size() ||
        inst.depots.size()  != depot_ids.size()  ||
        inst.lodgings.size() != lodging_ids.size())
        throw std::runtime_error("Los sets O/N/M no coinciden con los nodos leídos "
                                 "(faltan coordenadas o tarifas).");
    if (inst.days.empty())
        throw std::runtime_error("El set K (días) está vacío.");
    inst.build_index();

    // Validación mínima
    if (inst.bridges.empty())
        throw std::runtime_error("No se encontraron puentes en la instancia.");
    if (inst.depots.empty())
        throw std::runtime_error("No se encontraron depósitos en la instancia.");
    if (inst.Q <= 0)
        throw std::runtime_error("Parámetro Q inválido.");

    return inst;
}
