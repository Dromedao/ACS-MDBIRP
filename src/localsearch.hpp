#pragma once
#include "instance.hpp"
#include "solution.hpp"

// Operadores de búsqueda: tres vecindarios (2-opt, relocate, eliminate),
// cada uno genera soluciones vecinas y solo las acepta si siguen siendo
// factibles y bajan el costo. Se eligen con --ls-moves, se aplican con
// probabilidad --ls-prob y hacen a lo más --ls-iters rondas.
//
// Búsqueda local sobre una solución factible. Se aplica a la mejor hormiga de
// cada iteración del ACS. Cada movimiento se acepta solo si el resultado sigue
// cumpliendo el modelo (arcos existentes, turno <= Q, cadena de alojamientos,
// depósito de salida y regreso) y baja el costo de la Ec. (1).
//
//   2-opt     invierte un tramo de puentes dentro de un día. El inicio y el fin
//             del día no cambian, así que no se toca la cadena de alojamientos.
//   relocate  saca un puente de su día y lo inserta en cualquier posición de
//             cualquier día, del mismo equipo o de otro. Si el día queda vacío
//             se elimina; si la ruta completa queda vacía se elimina el equipo.
//   eliminate intenta repartir todos los puentes de un equipo en las rutas de
//             los demás (cada uno en su mejor posición). Si caben todos, el
//             equipo desaparece y se ahorra su despacho v, que es el término
//             más pesado del costo. Un movimiento de a un puente casi nunca
//             logra esto, porque cada paso por separado no baja el costo.

enum LsMoves { LS_NONE = 0, LS_2OPT = 1, LS_RELOCATE = 2, LS_ELIMINATE = 4, LS_ALL = 7 };

struct LsParams {  // PAUTA operadores parametrizados
    int moves     = LS_RELOCATE;  // qué vecindarios se usan (combinación de LsMoves); por defecto, solo relocate
    int max_rounds = 30;       // rondas de mejora como máximo
};

// Una ronda aplica a lo más una eliminación de equipo, una reubicación y un
// barrido completo de 2-opt.
// Devuelve cuántas rondas mejoraron la solución y deja `sol` reevaluada.
int local_search(Solution& sol, const Instance& inst, const LsParams& p);
