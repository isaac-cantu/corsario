#pragma once
// -----------------------------------------------------------------------------
// corsario/shower.hpp
//
// In-memory representation of one CORSIKA shower (one EVTH ... EVTE block).
// Unlike the original corsario implementation, a Shower here is a plain data
// container: it is filled once by Corsario's single sequential pass over the
// file (see corsario.hpp) and never re-opens or re-seeks the file itself.
// -----------------------------------------------------------------------------

#include <array>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "corsario/particle.hpp"

namespace corsario {

class Shower {
public:
    Shower() = default;
    explicit Shower(int index) : index_(index) {}

    int index() const { return index_; }  // 1-based shower number, in file order

    // ----- EVTH fields (Table 9) -----
    float event_no = 0.0f;
    float primary_id = 0.0f;
    float total_energy_gev = 0.0f;
    float starting_altitude_gcm2 = 0.0f;
    float first_target_no = 0.0f;
    float z_first_interaction_cm = 0.0f;
    float px_momentum = 0.0f;
    float py_momentum = 0.0f;
    float pz_momentum = 0.0f;
    float zenith_rad = 0.0f;
    float azimuth_rad = 0.0f;

    // ----- EVTE fields (Table 16), best-effort / optional -----
    bool has_event_end = false;
    float n_photons_at_obs = 0.0f;
    float n_electrons_at_obs = 0.0f;
    float n_hadrons_at_obs = 0.0f;
    float n_muons_at_obs = 0.0f;
    float n_particles_written = 0.0f;

    // Whether one or more 'LONG' longitudinal sub-blocks were skipped for
    // this shower (i.e. the run was produced with the LONGI option).
    bool has_longitudinal_blocks = false;

    // Whether this shower's DATA sub-blocks carried an 8th "weight" word
    // (i.e. the file was produced with the THIN option).
    bool thinned = false;

    std::vector<Particle> particles;

    int n_particles() const { return static_cast<int>(particles.size()); }
    const Particle& particle(int n) const { return particles.at(n); }

    // "event_no,particle_id,total_energy,altitude,no_target,z,px,py,pz,zenith,azimuth"
    std::string header_csv_row() const {
        std::ostringstream oss;
        oss.precision(std::numeric_limits<float>::max_digits10);
        oss << event_no << ',' << primary_id << ',' << total_energy_gev << ','
            << starting_altitude_gcm2 << ',' << first_target_no << ','
            << z_first_interaction_cm << ',' << px_momentum << ',' << py_momentum
            << ',' << pz_momentum << ',' << zenith_rad << ',' << azimuth_rad;
        return oss.str();
    }

private:
    int index_ = 0;
};

}  // namespace corsario
