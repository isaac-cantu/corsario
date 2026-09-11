#pragma once
// -----------------------------------------------------------------------------
// corsario/corsario.hpp
//
// Corsario: top-level reader for CORSIKA ".DAT" particle-output files.
//
// Design goals (see docs/FORMAT.md for the full rationale):
//   - Single sequential pass over the file: O(file size), one file handle,
//     no re-opening or re-seeking per shower. This is what actually made the
//     original reader slow on large runs.
//   - Auto-detects record markers and the THIN option (see format.hpp)
//     instead of assuming one fixed layout, which is what made the original
//     reader fail past a certain number of particles on real files.
//   - All word/byte offsets use `long` (not `int`) so files well beyond 2 GB
//     do not silently wrap around.
//   - `strict` mode (default) raises on malformed/truncated input; passing
//     strict=false degrades to best-effort parsing with warnings, which is
//     convenient when scanning many files produced by a batch farm where a
//     few jobs may have been killed mid-write.
// -----------------------------------------------------------------------------

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "corsario/format.hpp"
#include "corsario/particle.hpp"
#include "corsario/shower.hpp"
#include "corsario/stream.hpp"

namespace corsario {

inline const char* library_version() { return "1.1.0"; }

class Corsario {
public:
    explicit Corsario(const std::string& filename, bool strict = true)
        : filename_(filename), strict_(strict) {
        parse();
    }

    const std::string& filename() const { return filename_; }
    std::string version() const { return library_version(); }

    const FormatInfo& format() const { return format_; }
    bool is_thinned() const { return format_.thinning; }
    bool has_markers() const { return format_.has_markers; }

    long run_number() const { return run_number_; }
    long n_events_processed() const { return n_events_processed_; }

    int n_showers() const { return static_cast<int>(showers_.size()); }
    Shower& shower(int i) { return showers_.at(i); }
    const Shower& shower(int i) const { return showers_.at(i); }
    const std::vector<Shower>& showers() const { return showers_; }

    const std::vector<std::string>& warnings() const { return warnings_; }

    long total_particles() const {
        long total = 0;
        for (const auto& s : showers_) total += s.n_particles();
        return total;
    }

    // ----- CSV export --------------------------------------------------
    // Column names/order match the historical corsario output exactly, so
    // existing downstream code (e.g. ARCHES) does not need any changes.

    // "particle_id,px,py,pz,x,y,t"
    void to_csv(const std::string& path) const {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("corsario: cannot open " + path + " for writing");
        out << "particle_id,px,py,pz,x,y,t\n";
        for (const auto& sh : showers_)
            for (const auto& p : sh.particles) out << p.to_csv_row() << '\n';
    }

    // "shower,particle_type,no_particle,hadr_gen,no_obs,mass,energy,
    //  particle_id,px,py,pz,x,y,t,weight"
    // (weight is new: 1.0 for non-thinned files, the real THIN weight
    // otherwise; it is appended at the end so existing column-name based
    // access, e.g. pandas' df["x"], is unaffected.)
    void to_csv_ext(const std::string& path) const {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("corsario: cannot open " + path + " for writing");
        out << "shower,particle_type,no_particle,hadr_gen,no_obs,mass,energy,"
               "particle_id,px,py,pz,x,y,t,weight\n";
        for (const auto& sh : showers_) {
            for (const auto& p : sh.particles) {
                out << static_cast<long>(sh.event_no) << ',' << p.to_csv_row_ext() << ','
                    << p.weight() << '\n';
            }
        }
    }

    // "event_no,particle_id,total_energy,altitude,no_target,z,px,py,pz,zenith,azimuth"
    void shower_csv(const std::string& path) const {
        std::ofstream out(path);
        if (!out) throw std::runtime_error("corsario: cannot open " + path + " for writing");
        out << "event_no,particle_id,total_energy,altitude,no_target,z,px,py,pz,"
               "zenith,azimuth\n";
        for (const auto& sh : showers_) out << sh.header_csv_row() << '\n';
    }

    // ----- Flat (all-showers) accessors -----------------------------------
    // Used by the pybind11 layer to hand NumPy arrays back to Python in one
    // shot (for building DataFrames / JSON / HDF5) instead of looping over
    // millions of individual Python-level Particle objects.

    std::vector<int> flat_shower_index() const { return gather<int>([](const Shower& s, const Particle&) { return s.index(); }); }
    std::vector<long> flat_particle_id() const { return gather<long>([](const Shower&, const Particle& p) { return p.particle_id(); }); }
    std::vector<float> flat_px() const { return gather<float>([](const Shower&, const Particle& p) { return p.px(); }); }
    std::vector<float> flat_py() const { return gather<float>([](const Shower&, const Particle& p) { return p.py(); }); }
    std::vector<float> flat_pz() const { return gather<float>([](const Shower&, const Particle& p) { return p.pz(); }); }
    std::vector<float> flat_x() const { return gather<float>([](const Shower&, const Particle& p) { return p.x(); }); }
    std::vector<float> flat_y() const { return gather<float>([](const Shower&, const Particle& p) { return p.y(); }); }
    std::vector<float> flat_t() const { return gather<float>([](const Shower&, const Particle& p) { return p.time(); }); }
    std::vector<float> flat_energy() const { return gather<float>([](const Shower&, const Particle& p) { return p.energy(); }); }
    std::vector<float> flat_mass() const { return gather<float>([](const Shower&, const Particle& p) { return p.mass(); }); }
    std::vector<float> flat_weight() const { return gather<float>([](const Shower&, const Particle& p) { return p.weight(); }); }
    std::vector<int> flat_particle_no() const { return gather<int>([](const Shower&, const Particle& p) { return p.particle_no(); }); }
    std::vector<int> flat_hadron_gen() const { return gather<int>([](const Shower&, const Particle& p) { return p.hadron_generation(); }); }
    std::vector<int> flat_obs_level() const { return gather<int>([](const Shower&, const Particle& p) { return p.observation_level(); }); }
    std::vector<std::string> flat_particle_type() const { return gather<std::string>([](const Shower&, const Particle& p) { return p.particle_type(); }); }

private:
    std::string filename_;
    bool strict_;
    FormatInfo format_;
    std::vector<Shower> showers_;
    std::vector<std::string> warnings_;
    long run_number_ = -1;
    long n_events_processed_ = -1;

    template <typename T, typename Fn>
    std::vector<T> gather(Fn fn) const {
        std::vector<T> out;
        out.reserve(static_cast<std::size_t>(total_particles()));
        for (const auto& sh : showers_)
            for (const auto& p : sh.particles) out.push_back(fn(sh, p));
        return out;
    }

    void parse() {
        ShowerStream stream(filename_, strict_);
        while (!stream.done()) {
            auto batch = stream.next_batch(1 << 16);  // 65536 showers/chunk
            if (batch.empty()) break;
            for (auto& s : batch) showers_.push_back(std::move(s));
        }
        format_ = stream.format();
        run_number_ = stream.run_number();
        n_events_processed_ = stream.n_events_processed();
        warnings_ = stream.warnings();

        if (showers_.empty()) {
            warnings_.push_back("corsario: no complete showers found in " + filename_);
        }
    }
};

}  // namespace corsario
