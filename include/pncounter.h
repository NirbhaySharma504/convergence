// pncounter.h — increment/decrement counter built on two G-Counters (§6.3).
#ifndef CONVERGENCE_PNCOUNTER_H
#define CONVERGENCE_PNCOUNTER_H

#include "gcounter.h"

typedef struct {
    gcounter_t P; // all increments
    gcounter_t N; // all decrements
} pncounter_t;

void    pncounter_init      (pncounter_t *pc, int my_id, int num_nodes);
void    pncounter_increment (pncounter_t *pc);                          // P++
void    pncounter_decrement (pncounter_t *pc);                          // N++
int64_t pncounter_value     (const pncounter_t *pc);                    // P.value - N.value
void    pncounter_merge     (pncounter_t *dst, const pncounter_t *src); // merge P; merge N

#endif
