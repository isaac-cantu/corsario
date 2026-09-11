#pragma once
// -----------------------------------------------------------------------------
// corsario/particle.hpp
//
// Representation of a single CORSIKA secondary particle, decoded from a
// "particle data sub-block" record (CORSIKA_GUIDE, Table 10, Sect. 10.2).
//
// A particle record on disk is 7 (no thinning) or 8 (with thinning, +weight)
// single-precision floats:
//
//   [0] particle description = particle_id * 1000 + hadronic_generation * 10
//                               + observation_level
//   [1] px  (GeV/c)
//   [2] py  (GeV/c)
//   [3] pz  (GeV/c, negative = downward)
//   [4] x   (cm)
//   [5] y   (cm)
//   [6] t   (ns, time since first interaction)
//   [7] weight (only present when the THIN option was used)
//
// The particle id -> (name, mass) tables below follow the CORSIKA particle
// numbering scheme (CORSIKA_GUIDE, Table 4). Entries that CORSIKA never
// produces in the observation-level particle stream are left at 0 / "other".
// -----------------------------------------------------------------------------

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace corsario {

constexpr std::size_t kParticleTableSize = 205;

// Human readable name for each CORSIKA particle code.
inline const std::array<std::string, kParticleTableSize>& particle_names() {
    static const std::array<std::string, kParticleTableSize> table = [] {
        std::array<std::string, kParticleTableSize> arr{};
        arr.fill("other");
        arr[1] = "gamma";
        arr[2] = "e+";
        arr[3] = "e-";
        arr[4] = "none";
        arr[5] = "mu+";
        arr[6] = "mu-";
        arr[7] = "pi0";
        arr[8] = "pi+";
        arr[9] = "pi-";
        arr[10] = "K0_long";
        arr[11] = "K+";
        arr[12] = "K-";
        arr[13] = "n";
        arr[14] = "p";
        arr[15] = "antiproton";
        arr[16] = "K0_short";
        arr[17] = "eta";
        arr[18] = "Lambda";
        arr[19] = "Sigma+";
        arr[20] = "Sigma0";
        arr[21] = "Sigma-";
        arr[22] = "Xi0";
        arr[23] = "Xi-";
        arr[24] = "Omega-";
        arr[25] = "antineutron";
        arr[26] = "antiLambda";
        arr[27] = "antiSigma-";
        arr[28] = "antiSigma0";
        arr[29] = "antiSigma+";
        arr[30] = "antiXi0";
        arr[31] = "antiXi+";
        arr[32] = "antiOmega+";
        arr[48] = "eta*";      // eta(958) etc. approximations kept from legacy table
        arr[50] = "rho0";
        arr[51] = "rho+";
        arr[52] = "rho-";
        arr[62] = "K*0";
        arr[63] = "K*+";
        arr[64] = "K*-";
        arr[65] = "K*0bar";
        arr[66] = "nu_e";
        arr[67] = "antinu_e";
        arr[68] = "nu_mu";
        arr[69] = "antinu_mu";
        arr[116] = "D0";
        arr[117] = "D+";
        arr[118] = "D-";
        arr[119] = "D0bar";
        arr[120] = "Ds+";
        arr[121] = "Ds-";
        arr[122] = "eta_c";
        arr[123] = "D*0";
        arr[124] = "D*+";
        arr[125] = "D*-";
        arr[126] = "D*0bar";
        arr[130] = "J/psi";
        arr[131] = "tau+";
        arr[132] = "tau-";
        arr[133] = "nu_tau";
        arr[134] = "antinu_tau";
        arr[176] = "Sigma_c++";
        arr[177] = "Sigma_c+";
        arr[178] = "Sigma_c0";
        arr[184] = "Lambda_b0";
        return arr;
    }();
    return table;
}

// Rest mass in GeV/c^2 for each CORSIKA particle code (0 where unknown/unused).
inline const std::array<float, kParticleTableSize>& particle_masses() {
    static const std::array<float, kParticleTableSize> table = [] {
        std::array<float, kParticleTableSize> arr{};
        arr.fill(0.0f);
        arr[2] = 0.000511f;
        arr[3] = 0.000511f;
        arr[5] = 0.105658f;
        arr[6] = 0.105658f;
        arr[7] = 0.134977f;
        arr[8] = 0.139570f;
        arr[9] = 0.139570f;
        arr[10] = 0.497611f;
        arr[11] = 0.493677f;
        arr[12] = 0.493677f;
        arr[13] = 0.939565f;
        arr[14] = 0.938272f;
        arr[15] = 0.938272f;
        arr[16] = 0.497611f;
        arr[17] = 0.547862f;
        arr[18] = 1.115683f;
        arr[19] = 1.189370f;
        arr[20] = 1.192642f;
        arr[21] = 1.197449f;
        arr[22] = 1.314860f;
        arr[23] = 1.321710f;
        arr[24] = 1.672450f;
        arr[25] = 0.939565f;
        arr[26] = 1.115683f;
        arr[27] = 1.189370f;
        arr[28] = 1.192642f;
        arr[29] = 1.197449f;
        arr[30] = 1.314860f;
        arr[31] = 1.321710f;
        arr[32] = 1.672450f;
        arr[50] = 0.775260f;
        arr[51] = 0.775110f;
        arr[52] = 0.775110f;
        arr[62] = 0.895810f;
        arr[63] = 0.891660f;
        arr[64] = 0.891660f;
        arr[65] = 0.895810f;
        arr[116] = 1.864840f;
        arr[117] = 1.869660f;
        arr[118] = 1.869660f;
        arr[119] = 1.864840f;
        arr[120] = 1.968350f;
        arr[121] = 1.968350f;
        arr[122] = 2.983900f;
        arr[130] = 3.096900f;
        arr[131] = 1.776860f;
        arr[132] = 1.776860f;
        arr[176] = 2.453970f;
        arr[177] = 2.452900f;
        arr[178] = 2.453750f;
        arr[184] = 5.619600f;
        return arr;
    }();
    return table;
}

inline const std::string& particle_name_for(int code) {
    const auto& table = particle_names();
    if (code >= 0 && static_cast<std::size_t>(code) < table.size()) return table[code];
    return table[0];
}

inline float particle_mass_for(int code) {
    const auto& table = particle_masses();
    if (code >= 0 && static_cast<std::size_t>(code) < table.size()) return table[code];
    return table[0];
}

// -----------------------------------------------------------------------------
// Particle
// -----------------------------------------------------------------------------
class Particle {
public:
    Particle() = default;

    Particle(float description, float px, float py, float pz, float x, float y,
              float time, float weight = 1.0f)
        : description_(description),
          px_(px), py_(py), pz_(pz),
          x_(x), y_(y), time_(time), weight_(weight) {
        const long code = static_cast<long>(description_);
        particle_no_ = static_cast<int>(code / 1000);
        hadron_gen_  = static_cast<int>((code % 1000) / 10);
        obs_level_   = static_cast<int>(code % 10);
        mass_        = particle_mass_for(particle_no_);
        energy_      = std::sqrt(px_ * px_ + py_ * py_ + pz_ * pz_ + mass_ * mass_);
    }

    // Raw encoded description word (particle_id*1000 + hadr_gen*10 + obs_level)
    float description() const { return description_; }
    long  particle_id() const { return static_cast<long>(description_); }

    int particle_no() const { return particle_no_; }
    int hadron_generation() const { return hadron_gen_; }
    int observation_level() const { return obs_level_; }

    const std::string& particle_type() const { return particle_name_for(particle_no_); }

    float mass() const { return mass_; }
    float energy() const { return energy_; }

    float px() const { return px_; }
    float py() const { return py_; }
    float pz() const { return pz_; }
    std::array<float, 3> momentum() const { return {px_, py_, pz_}; }

    float x() const { return x_; }
    float y() const { return y_; }
    float time() const { return time_; }

    // Only meaningful when the file was produced with the THIN option;
    // defaults to 1.0 otherwise.
    float weight() const { return weight_; }

    // "particle_id,px,py,pz,x,y,t" — kept identical to the historical corsario
    // CSV so downstream consumers (e.g. ARCHES) do not need to change.
    // Uses max_digits10 (9 for float32) so the text round-trips to the
    // exact same bits -- the default ostream precision (6 significant
    // digits) silently truncates values, and would otherwise disagree
    // with the pandas-based streaming export path (iter_shower_batches),
    // which preserves full float32 precision.
    std::string to_csv_row() const {
        std::ostringstream oss;
        oss.precision(std::numeric_limits<float>::max_digits10);
        oss << particle_id() << ',' << px_ << ',' << py_ << ',' << pz_ << ','
            << x_ << ',' << y_ << ',' << time_;
        return oss.str();
    }

    // "particle_type,no_particle,hadr_gen,no_obs,mass,energy,particle_id,px,py,pz,x,y,t"
    std::string to_csv_row_ext() const {
        std::ostringstream oss;
        oss.precision(std::numeric_limits<float>::max_digits10);
        oss << particle_type() << ',' << particle_no_ << ',' << hadron_gen_ << ','
            << obs_level_ << ',' << mass_ << ',' << energy_ << ',' << particle_id()
            << ',' << px_ << ',' << py_ << ',' << pz_ << ',' << x_ << ',' << y_
            << ',' << time_;
        return oss.str();
    }

private:
    float description_ = 0.0f;
    float px_ = 0.0f, py_ = 0.0f, pz_ = 0.0f;
    float x_ = 0.0f, y_ = 0.0f, time_ = 0.0f;
    float weight_ = 1.0f;

    int particle_no_ = 0;
    int hadron_gen_ = 0;
    int obs_level_ = 0;
    float mass_ = 0.0f;
    float energy_ = 0.0f;
};

}  // namespace corsario
