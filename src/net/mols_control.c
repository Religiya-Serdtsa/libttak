#include <ttak/mols_control.h>

#define TTAK_MOLS_SYMBOL_BITS        (TTAK_MOLS_COORD_SHIFT)
#define TTAK_MOLS_COORD_MASK         ((1U << (TTAK_MOLS_SYMBOL_BITS * 2U)) - 1U)

static inline uint32_t ttak_mols_row(uint16_t node_id)
{
    return ((uint32_t)node_id >> TTAK_MOLS_COORD_SHIFT) & TTAK_MOLS_SYMBOL_MASK;
}

static inline uint32_t ttak_mols_col(uint16_t node_id)
{
    return (uint32_t)node_id & TTAK_MOLS_SYMBOL_MASK;
}

static inline uint32_t ttak_siamese_forward(uint32_t row, uint32_t col)
{
    return (row + col) & TTAK_MOLS_SYMBOL_MASK;
}

static inline uint32_t ttak_siamese_reverse(uint32_t row, uint32_t col)
{
    /* Reverse Siamese: reflect columns then apply complement over the symbol space. */
    const uint32_t mirrored_col =
        (TTAK_MOLS_SYMBOL_MASK - col) & TTAK_MOLS_SYMBOL_MASK;
    const uint32_t mirrored_symbol = (row + mirrored_col) & TTAK_MOLS_SYMBOL_MASK;
    return (TTAK_MOLS_SYMBOL_MASK - mirrored_symbol) & TTAK_MOLS_SYMBOL_MASK;
}

uint32_t ttak_apply_mols_control(uint16_t node_id, uint32_t current_load)
{
    const uint32_t row = ttak_mols_row(node_id);
    const uint32_t col = ttak_mols_col(node_id);
    const uint32_t siamese = ttak_siamese_forward(row, col);
    const uint32_t reverse = ttak_siamese_reverse(row, col);

    const uint32_t payload_row =
        (current_load >> TTAK_MOLS_COORD_SHIFT) & TTAK_MOLS_SYMBOL_MASK;
    const uint32_t payload_col = current_load & TTAK_MOLS_SYMBOL_MASK;

    const uint32_t mixed_row = (payload_row ^ siamese) & TTAK_MOLS_SYMBOL_MASK;
    const uint32_t mixed_col = (payload_col ^ reverse) & TTAK_MOLS_SYMBOL_MASK;

    return ((mixed_row << TTAK_MOLS_COORD_SHIFT) | mixed_col) &
           (uint32_t)TTAK_MOLS_COORD_MASK;
}
