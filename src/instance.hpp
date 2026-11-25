#pragma once
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <stdexcept>
#include <unordered_map>

// Nodos del problema

struct Depot {
    int    id;
    double x, y;   // coordenadas (lat, lon)
};

struct Bridge {
    int    id;
    double x, y;
    double w;       // tiempo de inspección (minutos)
    int    q;       // 1 = visitable (siempre 1 en instancias reales)
};

struct Lodging {
    int    id;
    double x, y;
    double rate;    // tarifa por noche (param f)
};

// Instancia completa

struct Instance {
    std::vector<Depot>   depots;
    std::vector<Bridge>  bridges;
    std::vector<Lodging> lodgings;
    std::vector<int>     days;      // set K, ej {1,2,3}

    int    D;   // parámetro auxiliar del generador
    double Q;   // duración máxima del turno diario (minutos)
    double v;   // costo de despacho por equipo

    // Matriz de distancia/tiempo de viaje entre cualquier par de nodos,
    // tal como se lee del archivo. Indexada por (id_origen, id_destino). Cubre:
    //   puente-puente, puente-alojamiento, alojamiento-puente,
    //   depósito-puente, puente-depósito.
    // (No existen arcos depósito-depósito, alojamiento-alojamiento ni
    //  alojamiento-depósito: coincide con el conjunto de arcos del modelo.)
    std::map<std::pair<int,int>, double> dist;

    // Versión densa de `dist` para consulta O(1) en la construcción de rutas.
    // node_idx[id] = posición del nodo (-1 si el id no existe); dm[i*n_nodes+j]
    // = distancia, o -1 si el arco no existe. Se llena en build_index().
    int                 n_nodes = 0;
    std::vector<int>    node_idx;
    std::vector<double> dm;
    void build_index();

    int  idx(int id) const { return node_idx.at(id); }   // .at: falla con excepción si el id no existe
    bool has_arc(int from, int to) const;

    // Acceso seguro a distancia (lanza si no existe)
    double get_dist(int from, int to) const;

    // Acceso a tarifa de alojamiento por id
    double get_lodging_rate(int lodging_id) const;

    // Helpers de búsqueda por id (O(1), usan los índices de build_index)
    const Depot*   find_depot(int id)   const;
    const Bridge*  find_bridge(int id)  const;
    const Lodging* find_lodging(int id) const;

    // id -> posición en depots/bridges/lodgings (-1 si no corresponde)
    std::vector<int> depot_pos, bridge_pos, lodging_pos;
};

// Parser principal (implementado en instance.cpp)

Instance parse_instance(const std::string& filepath);
