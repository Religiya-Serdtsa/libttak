# Mathematical and Historical References for libttak Algorithms

This document preserves the academic and historical lineage of the mathematical principles applied in libttak. To keep the source code mechanically readable and free of obscure cultural naming, all functions, variables, and comments use generic, descriptive system terminology. The references below are maintained for researchers who wish to trace the origins of specific algorithms.

---

## `ttak_math_lane_mul` (formerly `ttak_math_dawonsul_lane_mul`)

- **Historical concept:** Dawonsul (多變數獨立法) — independent lane processing for multivariate linear systems, associated with the works of Hong Jeong-ha and collaborators.
- **Reference:** Hong Jeong-ha, *"Guiljip (九一集)"*, 1660s.
- **Application in code:** Matrix-vector lane multiplication used in `ttak_matrix_multiply_vec`. Each variable lane is processed independently to maximize throughput via aligned-limb FMA-style accumulation.

---

## `ttak_matrix_set_ols_magic_square_4x4` (formerly `ttak_matrix_set_gusuryak_4x4`)

- **Historical concept:** Mutually orthogonal Latin squares combined into a normal 0-15 magic square, documented in Choi Seok-jeong's work on combinatorial design.
- **Reference:** Choi Seok-jeong, *"Gusuryak (九數略)"*, 1700.
- **Application in code:** Initializes a 4×4 matrix as an Orthogonal Latin Square (OLS) whose rows, columns, and diagonals sum to 30. Used for deterministic, collision-minimizing grid-based data placement.

---

## `ttak_math_approx_sin` and `ttak_math_approx_cos`

- **Historical concept:** Polynomial approximation methods for trigonometric functions cataloged in the *Yussigihae* tradition.
- **Reference:** Nam Byeong-gil, *"Sanhak Jeong-ui (算學正義)"*, 1849.
- **Application in code:** Fixed-point polynomial expansions for approximate sine and cosine, used in `ttak_matrix_set_rotation` to compute stable rotation matrix entries.

---

## `ttak_bigreal_op_aligned_addsub` (formerly `ttak_bigreal_op_cheonwonsul`)

- **Historical concept:** Cheonwonsul (天元術), a system of aligned limb processing reinterpreted from Tian Yuan Shu for big-integer arithmetic.
- **Reference:** Hong Jeong-ha, *"Guiljip (九一集)"*, 1660s.
- **Application in code:** Internal helper for `ttak_bigreal_add` and `ttak_bigreal_sub`. Performs mantissa addition or subtraction after exponent alignment, mapping limbs to cache-line-friendly layouts.

---

## Latin-Square Scatter LUT in `src/mem/arena_helper.c`

- **Historical concept:** Latin-square offsets for epoch scattering.
- **Reference:** Choi Seok-jeong, *"Gusuryak (九數略)"*, 1700.
- **Application in code:** `ttak_arena_scatter_offset` uses an 8×8 Latin-square lookup table (`ttak_arena_latin_square_lut`) to scatter epoch allocations and minimize cache-line reuse across generations.

---

## OLS Traversal in `src/container/pool.c`

- **Historical concept:** Orthogonal Latin lattice selection for deterministic slot traversal.
- **Reference:** Choi Seok-jeong, *"Gusuryak (九數略)"*, 1700.
- **Application in code:** `ttak_object_pool_alloc` traverses pool slots via an order-8 Orthogonal Latin Square (OLS) to avoid clustering and reduce bitmap-scan contention.

---

## Lock-Free Lattice Ingress in `src/net/lattice.c`

- **Historical concept:** Deterministic coordinate scheduling on a 2-D lattice (Sanpan) for parallel ingress.
- **References:**
  - Choi Seok-jeong, *"Gusuryak (九數略)"*, 1700.
  - Yi Sang-hyeok, *"Suri (數理)"*, 1890s.
- **Application in code:** `ttak_net_lattice_write` performs lock-free deterministic writes by sweeping lattice coordinates in a deterministic order derived from OLS-based scheduling rules.

---

## `ttak_apply_mols_control` in `include/ttak/mols_control.h`

- **Historical concept:** Mutually Orthogonal Latin Squares (MOLS) applied to load redistribution.
- **Reference:** Reverse Siamese Latin-square constructions.
- **Application in code:** Congestion-control helper that redistributes load across a 64×64 mesh. Node coordinates feed a matched pair of Latin squares whose values are XOR-mixed with the caller's seed to produce deterministic shuffling.

---

## Buddy Allocator Residue Lookup in `src/phys/mem/buddy.c`

- **Historical concept:** Residue-class lookup tables for size-class indexing.
- **Reference:** Nam Byeong-gil, *"Sanhak Jeong-ui (算學正義)"*, 1849.
- **Application in code:** `select_block` uses a bitmask-based residue lookup to choose the appropriate buddy block order, reducing fragmentation through deterministic size-class selection.
