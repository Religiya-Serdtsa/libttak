#ifndef TTAK_PHYS_DIMLESS_TRANSPORT_H
#define TTAK_PHYS_DIMLESS_TRANSPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Status codes returned by transport dimensionless helpers.
 */
typedef enum ttak_phys_status {
    TTAK_PHYS_SUCCESS = 0,
    TTAK_PHYS_ERR_INVALID_ARGUMENT,
    TTAK_PHYS_ERR_DIVIDE_BY_ZERO,
    TTAK_PHYS_ERR_OUT_OF_RANGE
} ttak_phys_status_t;

/**
 * @brief Reynolds number: Re = rho * |v| * L / mu.
 *
 * @param[out] out_re Computed Reynolds number (dimensionless).
 * @param rho Fluid density in kg/m^3.
 * @param velocity Characteristic velocity magnitude in m/s.
 * @param length Characteristic length scale in meters.
 * @param dynamic_viscosity Dynamic viscosity in Pa*s (kg/(m*s)).
 */
ttak_phys_status_t ttak_phys_calc_reynolds(
    double *out_re,
    double rho,
    double velocity,
    double length,
    double dynamic_viscosity);

/**
 * @brief Schmidt number: Sc = mu / (rho * D).
 *
 * @param[out] out_sc Computed Schmidt number (dimensionless).
 * @param rho Fluid density in kg/m^3.
 * @param dynamic_viscosity Dynamic viscosity in Pa*s.
 * @param diffusivity Binary mass diffusivity in m^2/s.
 */
ttak_phys_status_t ttak_phys_calc_schmidt(
    double *out_sc,
    double rho,
    double dynamic_viscosity,
    double diffusivity);

/**
 * @brief Péclet number for mass transport: Pe = |v| * L / D.
 *
 * @param[out] out_pe Computed Péclet number (dimensionless).
 * @param velocity Characteristic velocity magnitude in m/s.
 * @param length Characteristic length in meters.
 * @param diffusivity Binary mass diffusivity in m^2/s.
 */
ttak_phys_status_t ttak_phys_calc_mass_peclet(
    double *out_pe,
    double velocity,
    double length,
    double diffusivity);

/**
 * @brief Sherwood number: Sh = k_m * L / D.
 *
 * @param[out] out_sh Computed Sherwood number (dimensionless).
 * @param mass_transfer_coeff Film mass-transfer coefficient k_m in m/s.
 * @param length Characteristic length in meters.
 * @param diffusivity Binary mass diffusivity in m^2/s.
 */
ttak_phys_status_t ttak_phys_calc_sherwood(
    double *out_sh,
    double mass_transfer_coeff,
    double length,
    double diffusivity);

/**
 * @brief Computes k_m from a Sherwood number: k_m = Sh * D / L.
 *
 * @param[out] out_km Derived film mass-transfer coefficient in m/s.
 * @param sherwood Sherwood number (dimensionless).
 * @param length Characteristic length in meters.
 * @param diffusivity Binary mass diffusivity in m^2/s.
 */
ttak_phys_status_t ttak_phys_calc_km(
    double *out_km,
    double sherwood,
    double length,
    double diffusivity);

#ifdef __cplusplus
}
#endif

#endif /* TTAK_PHYS_DIMLESS_TRANSPORT_H */
