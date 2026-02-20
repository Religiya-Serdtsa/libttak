#include <ttak/phys/dimless/transport.h>

#include <math.h>

static ttak_phys_status_t ttak_phys_require_output(double *out) {
    if (!out) {
        return TTAK_PHYS_ERR_INVALID_ARGUMENT;
    }
    return TTAK_PHYS_SUCCESS;
}

static ttak_phys_status_t ttak_phys_validate_positive(double value) {
    if (!isfinite(value)) {
        return TTAK_PHYS_ERR_INVALID_ARGUMENT;
    }
    if (value <= 0.0) {
        return TTAK_PHYS_ERR_OUT_OF_RANGE;
    }
    return TTAK_PHYS_SUCCESS;
}

static ttak_phys_status_t ttak_phys_validate_real(double value) {
    if (!isfinite(value)) {
        return TTAK_PHYS_ERR_INVALID_ARGUMENT;
    }
    return TTAK_PHYS_SUCCESS;
}

static ttak_phys_status_t ttak_phys_validate_non_negative(double value) {
    if (!isfinite(value)) {
        return TTAK_PHYS_ERR_INVALID_ARGUMENT;
    }
    if (value < 0.0) {
        return TTAK_PHYS_ERR_OUT_OF_RANGE;
    }
    return TTAK_PHYS_SUCCESS;
}

static ttak_phys_status_t ttak_phys_validate_divisor(double value) {
    if (!isfinite(value)) {
        return TTAK_PHYS_ERR_INVALID_ARGUMENT;
    }
    if (value == 0.0) {
        return TTAK_PHYS_ERR_DIVIDE_BY_ZERO;
    }
    if (value < 0.0) {
        return TTAK_PHYS_ERR_OUT_OF_RANGE;
    }
    return TTAK_PHYS_SUCCESS;
}

ttak_phys_status_t ttak_phys_calc_reynolds(
    double *out_re,
    double rho,
    double velocity,
    double length,
    double dynamic_viscosity) {
    ttak_phys_status_t status = ttak_phys_require_output(out_re);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(rho);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(length);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_divisor(dynamic_viscosity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_real(velocity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    const double v_mag = fabs(velocity);
    *out_re = (rho * v_mag * length) / dynamic_viscosity;
    return TTAK_PHYS_SUCCESS;
}

ttak_phys_status_t ttak_phys_calc_schmidt(
    double *out_sc,
    double rho,
    double dynamic_viscosity,
    double diffusivity) {
    ttak_phys_status_t status = ttak_phys_require_output(out_sc);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(rho);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_divisor(diffusivity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(dynamic_viscosity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    *out_sc = dynamic_viscosity / (rho * diffusivity);
    return TTAK_PHYS_SUCCESS;
}

ttak_phys_status_t ttak_phys_calc_mass_peclet(
    double *out_pe,
    double velocity,
    double length,
    double diffusivity) {
    ttak_phys_status_t status = ttak_phys_require_output(out_pe);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_divisor(diffusivity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(length);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_real(velocity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    const double v_mag = fabs(velocity);
    *out_pe = (v_mag * length) / diffusivity;
    return TTAK_PHYS_SUCCESS;
}

ttak_phys_status_t ttak_phys_calc_sherwood(
    double *out_sh,
    double mass_transfer_coeff,
    double length,
    double diffusivity) {
    ttak_phys_status_t status = ttak_phys_require_output(out_sh);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_non_negative(mass_transfer_coeff);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(length);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_divisor(diffusivity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    *out_sh = (mass_transfer_coeff * length) / diffusivity;
    return TTAK_PHYS_SUCCESS;
}

ttak_phys_status_t ttak_phys_calc_prandtl(
    double *out_pr,
    double dynamic_viscosity,
    double specific_heat,
    double thermal_conductivity) {
    ttak_phys_status_t status = ttak_phys_require_output(out_pr);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_divisor(thermal_conductivity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(dynamic_viscosity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(specific_heat);
    if (status != TTAK_PHYS_SUCCESS) return status;

    *out_pr = (specific_heat * dynamic_viscosity) / thermal_conductivity;
    return TTAK_PHYS_SUCCESS;
}

ttak_phys_status_t ttak_phys_calc_grashof(
    double *out_gr,
    double gravity,
    double expansion_coeff,
    double delta_temp,
    double length,
    double rho,
    double dynamic_viscosity) {
    ttak_phys_status_t status = ttak_phys_require_output(out_gr);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_divisor(dynamic_viscosity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(rho);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(length);
    if (status != TTAK_PHYS_SUCCESS) return status;

    double kinematic_viscosity = dynamic_viscosity / rho;
    *out_gr = (gravity * expansion_coeff * delta_temp * pow(length, 3.0)) / (kinematic_viscosity * kinematic_viscosity);
    return TTAK_PHYS_SUCCESS;
}

ttak_phys_status_t ttak_phys_calc_grashof_mass(
    double *out_grm,
    double gravity,
    double expansion_coeff_mass,
    double delta_conc,
    double length,
    double rho,
    double dynamic_viscosity) {
    ttak_phys_status_t status = ttak_phys_require_output(out_grm);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_divisor(dynamic_viscosity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(rho);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(length);
    if (status != TTAK_PHYS_SUCCESS) return status;

    double kinematic_viscosity = dynamic_viscosity / rho;
    *out_grm = (gravity * expansion_coeff_mass * delta_conc * pow(length, 3.0)) / (kinematic_viscosity * kinematic_viscosity);
    return TTAK_PHYS_SUCCESS;
}

ttak_phys_status_t ttak_phys_calc_rayleigh(
    double *out_ra,
    double grashof,
    double pr_or_sc) {
    ttak_phys_status_t status = ttak_phys_require_output(out_ra);
    if (status != TTAK_PHYS_SUCCESS) return status;

    *out_ra = grashof * pr_or_sc;
    return TTAK_PHYS_SUCCESS;
}

ttak_phys_status_t ttak_phys_calc_km(
    double *out_km,
    double sherwood,
    double length,
    double diffusivity) {
    ttak_phys_status_t status = ttak_phys_require_output(out_km);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_non_negative(sherwood);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_divisor(length);
    if (status != TTAK_PHYS_SUCCESS) return status;

    status = ttak_phys_validate_positive(diffusivity);
    if (status != TTAK_PHYS_SUCCESS) return status;

    *out_km = (sherwood * diffusivity) / length;
    return TTAK_PHYS_SUCCESS;
}
