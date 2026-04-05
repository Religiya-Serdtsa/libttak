#ifndef TTAK_MOLS_CONTROL_H
#define TTAK_MOLS_CONTROL_H

#include <stdint.h>

/**
 * @file mols_control.h
 * @brief Congestion control helpers built on Mutually Orthogonal Latin Squares.
 *
 * The helper exposes a single entry point that redistributes load across a
 * 64×64 mesh.  The routine leverages the existing linear (cyclic-shift) Latin
 * square when USE_NONLINEAR is disabled.  When enabled, the routine applies a
 * fixed-point inverse mapping table to dampen hot nodes via a non-linear curve
 * f(x) = 1/(1+x) without evaluating expensive math functions at runtime.
 */

#ifndef USE_NONLINEAR
#define USE_NONLINEAR 0
#endif

#define TTAK_MOLS_GRID_ORDER          (64U)
#define TTAK_MOLS_COORD_SHIFT         (6U)
#define TTAK_MOLS_SYMBOL_MASK         (TTAK_MOLS_GRID_ORDER - 1U)
#define TTAK_MOLS_NODE_COUNT          (TTAK_MOLS_GRID_ORDER * TTAK_MOLS_GRID_ORDER)
#define TTAK_MOLS_LUT_BITS            (8U)
#define TTAK_MOLS_LUT_SIZE            (1U << TTAK_MOLS_LUT_BITS)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply MOLS-based congestion control on a 64×64 mesh node.
 *
 * @param node_id      Linearized node identifier in [0, 4096).
 * @param current_load Arbitrary load metric (fixed-point friendly).
 * @return Adjusted load after linear or non-linear redistribution.
 */
uint32_t ttak_apply_mols_control(uint16_t node_id, uint32_t current_load);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_MOLS_CONTROL_H */
