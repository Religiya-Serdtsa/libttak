#ifndef TTAK_MOLS_CONTROL_H
#define TTAK_MOLS_CONTROL_H

#include <stdint.h>

/**
 * @file mols_control.h
 * @brief Congestion control helpers built on Reverse Siamese Latin squares.
 *
 * The helper exposes a single entry point that redistributes load across a
 * 64×64 mesh.  Each node's coordinates feed a matched pair of Latin squares:
 * the traditional Siamese traversal and its reverse (column reflection followed
 * by the complement transformation).  The pair is XOR-mixed with the caller's
 * seed so every subsystem observes the same Reverse Siamese shuffling without
 * branching on compile-time knobs.
 */

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
 * @return Reverse-Siamese mixed load coordinate.
 */
uint32_t ttak_apply_mols_control(uint16_t node_id, uint32_t current_load);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_MOLS_CONTROL_H */
