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
 * @brief Prandtl number: Pr = nu / alpha = cp * mu / k.
 *
 * @param[out] out_pr Computed Prandtl number.
 * @param dynamic_viscosity mu in Pa*s.
 * @param specific_heat cp in J/(kg*K).
 * @param thermal_conductivity k in W/(m*K).
 */
ttak_phys_status_t ttak_phys_calc_prandtl(
    double *out_pr,
    double dynamic_viscosity,
    double specific_heat,
    double thermal_conductivity);

/**
 * @brief Grashof number (Thermal): Gr = g * beta * (Ts - Tinf) * L^3 / nu^2.
 *
 * @param[out] out_gr Computed Grashof number.
 * @param gravity g in m/s^2.
 * @param expansion_coeff beta in 1/K.
 * @param delta_temp (Ts - Tinf) in K.
 * @param length L in meters.
 * @param rho Fluid density in kg/m^3.
 * @param dynamic_viscosity mu in Pa*s.
 */
ttak_phys_status_t ttak_phys_calc_grashof(
    double *out_gr,
    double gravity,
    double expansion_coeff,
    double delta_temp,
    double length,
    double rho,
    double dynamic_viscosity);

/**
 * @brief Grashof number (Mass): Gr_m = g * beta_m * (Cs - Cinf) * L^3 / nu^2.
 */
ttak_phys_status_t ttak_phys_calc_grashof_mass(
    double *out_grm,
    double gravity,
    double expansion_coeff_mass,
    double delta_conc,
    double length,
    double rho,
    double dynamic_viscosity);

/**
 * @brief Rayleigh number: Ra = Gr * Pr (Thermal) or Gr_m * Sc (Mass).
 */
ttak_phys_status_t ttak_phys_calc_rayleigh(
    double *out_ra,
    double grashof,
    double pr_or_sc);

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
