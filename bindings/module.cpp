// -----------------------------------------------------------------------------
// bindings/module.cpp
//
// pybind11 bindings for corsario. Exposes:
//   corsario.open(path, strict=True) -> Corsario
//   corsario.Corsario(path, strict=True)
//   corsario.Shower / corsario.Particle
//
// Bulk (NumPy) accessors are exposed as *_array() variants that return
// numpy arrays built with pybind11/stl (as plain Python lists converted to
// arrays on the Python side, see python/corsario/__init__.py) — see
// docs/API.md for the full surface.
// -----------------------------------------------------------------------------
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "corsario/corsario.hpp"
#include "corsario/format.hpp"
#include "corsario/particle.hpp"
#include "corsario/shower.hpp"
#include "corsario/stream.hpp"

namespace py = pybind11;
using namespace corsario;

namespace {

// Shared by Corsario.to_dataframe() (Python side) and the streaming
// iter_shower_batches() generator, so both paths build identical
// DataFrames from identical logic -- important since large-file users are
// expected to rely on the streaming path exclusively (see docs/LARGE_FILES.md).
template <typename T, typename Fn>
std::vector<T> gather(const std::vector<Shower>& showers, Fn fn) {
    std::size_t total = 0;
    for (const auto& s : showers) total += s.particles.size();
    std::vector<T> out;
    out.reserve(total);
    for (const auto& sh : showers)
        for (const auto& p : sh.particles) out.push_back(fn(sh, p));
    return out;
}

py::dict particles_dict(const std::vector<Shower>& showers) {
    py::dict d;
    d["shower"] = gather<int>(showers, [](const Shower& s, const Particle&) { return s.index(); });
    d["particle_type"] = gather<std::string>(showers, [](const Shower&, const Particle& p) { return p.particle_type(); });
    d["no_particle"] = gather<int>(showers, [](const Shower&, const Particle& p) { return p.particle_no(); });
    d["hadr_gen"] = gather<int>(showers, [](const Shower&, const Particle& p) { return p.hadron_generation(); });
    d["no_obs"] = gather<int>(showers, [](const Shower&, const Particle& p) { return p.observation_level(); });
    d["mass"] = gather<float>(showers, [](const Shower&, const Particle& p) { return p.mass(); });
    d["energy"] = gather<float>(showers, [](const Shower&, const Particle& p) { return p.energy(); });
    d["particle_id"] = gather<long>(showers, [](const Shower&, const Particle& p) { return p.particle_id(); });
    d["px"] = gather<float>(showers, [](const Shower&, const Particle& p) { return p.px(); });
    d["py"] = gather<float>(showers, [](const Shower&, const Particle& p) { return p.py(); });
    d["pz"] = gather<float>(showers, [](const Shower&, const Particle& p) { return p.pz(); });
    d["x"] = gather<float>(showers, [](const Shower&, const Particle& p) { return p.x(); });
    d["y"] = gather<float>(showers, [](const Shower&, const Particle& p) { return p.y(); });
    d["t"] = gather<float>(showers, [](const Shower&, const Particle& p) { return p.time(); });
    d["weight"] = gather<float>(showers, [](const Shower&, const Particle& p) { return p.weight(); });
    return d;
}

py::dict shower_headers_dict(const std::vector<Shower>& showers) {
    const std::size_t n = showers.size();
    std::vector<float> event_no(n), primary_id(n), total_energy(n), altitude(n), no_target(n),
        z(n), px(n), py_(n), pz(n), zenith(n), azimuth(n), n_photons(n), n_electrons(n),
        n_hadrons(n), n_muons(n);
    std::vector<int> n_particles(n);
    std::vector<bool> has_long(n), thinned(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto& s = showers[i];
        event_no[i] = s.event_no;
        primary_id[i] = s.primary_id;
        total_energy[i] = s.total_energy_gev;
        altitude[i] = s.starting_altitude_gcm2;
        no_target[i] = s.first_target_no;
        z[i] = s.z_first_interaction_cm;
        px[i] = s.px_momentum;
        py_[i] = s.py_momentum;
        pz[i] = s.pz_momentum;
        zenith[i] = s.zenith_rad;
        azimuth[i] = s.azimuth_rad;
        n_particles[i] = s.n_particles();
        n_photons[i] = s.n_photons_at_obs;
        n_electrons[i] = s.n_electrons_at_obs;
        n_hadrons[i] = s.n_hadrons_at_obs;
        n_muons[i] = s.n_muons_at_obs;
        has_long[i] = s.has_longitudinal_blocks;
        thinned[i] = s.thinned;
    }
    py::dict d;
    d["event_no"] = event_no;
    d["particle_id"] = primary_id;
    d["total_energy"] = total_energy;
    d["altitude"] = altitude;
    d["no_target"] = no_target;
    d["z"] = z;
    d["px"] = px;
    d["py"] = py_;
    d["pz"] = pz;
    d["zenith"] = zenith;
    d["azimuth"] = azimuth;
    d["n_particles"] = n_particles;
    d["n_photons_at_obs"] = n_photons;
    d["n_electrons_at_obs"] = n_electrons;
    d["n_hadrons_at_obs"] = n_hadrons;
    d["n_muons_at_obs"] = n_muons;
    d["has_longitudinal_blocks"] = has_long;
    d["thinned"] = thinned;
    return d;
}

}  // namespace

PYBIND11_MODULE(_corsario_core, m) {
    m.doc() = "corsario: fast reader for CORSIKA .DAT particle-output files";

    m.attr("__version__") = library_version();

    py::class_<FormatInfo>(m, "FormatInfo")
        .def_readonly("has_markers", &FormatInfo::has_markers)
        .def_readonly("thinning", &FormatInfo::thinning)
        .def_readonly("block_words", &FormatInfo::block_words)
        .def_readonly("words_per_particle", &FormatInfo::words_per_particle)
        .def("__repr__", [](const FormatInfo& f) {
            return "<FormatInfo markers=" + std::string(f.has_markers ? "True" : "False") +
                   " thinning=" + std::string(f.thinning ? "True" : "False") +
                   " block_words=" + std::to_string(f.block_words) + ">";
        });

    py::class_<Particle>(m, "Particle")
        .def_property_readonly("particle_id", &Particle::particle_id)
        .def_property_readonly("particle_no", &Particle::particle_no)
        .def_property_readonly("particle_type", &Particle::particle_type)
        .def_property_readonly("hadron_generation", &Particle::hadron_generation)
        .def_property_readonly("observation_level", &Particle::observation_level)
        .def_property_readonly("mass", &Particle::mass)
        .def_property_readonly("energy", &Particle::energy)
        .def_property_readonly("px", &Particle::px)
        .def_property_readonly("py", &Particle::py)
        .def_property_readonly("pz", &Particle::pz)
        .def_property_readonly("momentum", &Particle::momentum)
        .def_property_readonly("x", &Particle::x)
        .def_property_readonly("y", &Particle::y)
        .def_property_readonly("time", &Particle::time)
        .def_property_readonly("weight", &Particle::weight)
        .def("to_csv_row", &Particle::to_csv_row)
        .def("to_csv_row_ext", &Particle::to_csv_row_ext)
        .def("__repr__", [](const Particle& p) {
            return "<Particle id=" + std::to_string(p.particle_id()) +
                   " type='" + p.particle_type() + "' energy=" +
                   std::to_string(p.energy()) + " GeV>";
        });

    py::class_<Shower>(m, "Shower")
        .def_property_readonly("index", &Shower::index)
        .def_readonly("event_no", &Shower::event_no)
        .def_readonly("primary_id", &Shower::primary_id)
        .def_readonly("total_energy_gev", &Shower::total_energy_gev)
        .def_readonly("starting_altitude_gcm2", &Shower::starting_altitude_gcm2)
        .def_readonly("first_target_no", &Shower::first_target_no)
        .def_readonly("z_first_interaction_cm", &Shower::z_first_interaction_cm)
        .def_readonly("px_momentum", &Shower::px_momentum)
        .def_readonly("py_momentum", &Shower::py_momentum)
        .def_readonly("pz_momentum", &Shower::pz_momentum)
        .def_readonly("zenith_rad", &Shower::zenith_rad)
        .def_readonly("azimuth_rad", &Shower::azimuth_rad)
        .def_readonly("has_event_end", &Shower::has_event_end)
        .def_readonly("n_photons_at_obs", &Shower::n_photons_at_obs)
        .def_readonly("n_electrons_at_obs", &Shower::n_electrons_at_obs)
        .def_readonly("n_hadrons_at_obs", &Shower::n_hadrons_at_obs)
        .def_readonly("n_muons_at_obs", &Shower::n_muons_at_obs)
        .def_readonly("n_particles_written", &Shower::n_particles_written)
        .def_readonly("has_longitudinal_blocks", &Shower::has_longitudinal_blocks)
        .def_readonly("thinned", &Shower::thinned)
        .def("n_particles", &Shower::n_particles)
        .def("particle", &Shower::particle)
        .def("particles", [](const Shower& s) { return s.particles; })
        .def("header_csv_row", &Shower::header_csv_row)
        .def("__len__", &Shower::n_particles)
        .def("__getitem__", &Shower::particle)
        .def("__repr__", [](const Shower& s) {
            return "<Shower #" + std::to_string(s.index()) + " event_no=" +
                   std::to_string(static_cast<long>(s.event_no)) + " n_particles=" +
                   std::to_string(s.n_particles()) + ">";
        });

    py::class_<Corsario>(m, "Corsario")
        .def(py::init<const std::string&, bool>(), py::arg("filename"), py::arg("strict") = true)
        .def_property_readonly("filename", &Corsario::filename)
        .def_property_readonly("version", &Corsario::version)
        .def_property_readonly("format", &Corsario::format)
        .def_property_readonly("is_thinned", &Corsario::is_thinned)
        .def_property_readonly("has_markers", &Corsario::has_markers)
        .def_property_readonly("run_number", &Corsario::run_number)
        .def_property_readonly("n_events_processed", &Corsario::n_events_processed)
        .def_property_readonly("warnings", &Corsario::warnings)
        .def("n_showers", &Corsario::n_showers)
        .def("shower", static_cast<Shower& (Corsario::*)(int)>(&Corsario::shower),
             py::return_value_policy::reference_internal)
        .def("showers", [](const Corsario& c) { return c.showers(); })
        .def("total_particles", &Corsario::total_particles)
        .def("to_csv", &Corsario::to_csv, py::arg("path"))
        .def("to_csv_ext", &Corsario::to_csv_ext, py::arg("path"))
        .def("shower_csv", &Corsario::shower_csv, py::arg("path"))
        .def("flat_shower_index", &Corsario::flat_shower_index)
        .def("flat_particle_id", &Corsario::flat_particle_id)
        .def("flat_px", &Corsario::flat_px)
        .def("flat_py", &Corsario::flat_py)
        .def("flat_pz", &Corsario::flat_pz)
        .def("flat_x", &Corsario::flat_x)
        .def("flat_y", &Corsario::flat_y)
        .def("flat_t", &Corsario::flat_t)
        .def("flat_energy", &Corsario::flat_energy)
        .def("flat_mass", &Corsario::flat_mass)
        .def("flat_weight", &Corsario::flat_weight)
        .def("flat_particle_no", &Corsario::flat_particle_no)
        .def("flat_hadron_gen", &Corsario::flat_hadron_gen)
        .def("flat_obs_level", &Corsario::flat_obs_level)
        .def("flat_particle_type", &Corsario::flat_particle_type)
        .def("__len__", &Corsario::n_showers)
        .def("__getitem__", static_cast<Shower& (Corsario::*)(int)>(&Corsario::shower),
             py::return_value_policy::reference_internal)
        .def("__enter__", [](Corsario& self) -> Corsario& { return self; })
        .def("__exit__", [](Corsario&, py::object, py::object, py::object) {})
        .def("__repr__", [](const Corsario& c) {
            return "<Corsario '" + c.filename() + "' n_showers=" +
                   std::to_string(c.n_showers()) + " thinned=" +
                   (c.is_thinned() ? "True" : "False") + ">";
        });

    py::class_<ShowerStream>(m, "ShowerStream")
        .def(py::init<const std::string&, bool>(), py::arg("filename"), py::arg("strict") = true,
             "Resumable reader for files too large to hold entirely in memory. "
             "See corsario.iter_shower_batches().")
        .def("done", &ShowerStream::done)
        .def("next_batch", &ShowerStream::next_batch, py::arg("max_showers"))
        .def_property_readonly("filename", &ShowerStream::filename)
        .def_property_readonly("format", &ShowerStream::format)
        .def_property_readonly("is_thinned", &ShowerStream::is_thinned)
        .def_property_readonly("has_markers", &ShowerStream::has_markers)
        .def_property_readonly("run_number", &ShowerStream::run_number)
        .def_property_readonly("n_events_processed", &ShowerStream::n_events_processed)
        .def_property_readonly("warnings", &ShowerStream::warnings);

    m.def("_particles_dict", &particles_dict,
          "Build a {column: list} dict of particle-level fields from a list "
          "of Shower objects (internal; used by to_dataframe()/iter_shower_batches()).");
    m.def("_shower_headers_dict", &shower_headers_dict,
          "Build a {column: list} dict of shower-header fields from a list "
          "of Shower objects (internal; used by to_dataframe()/iter_shower_batches()).");

    m.def(
        "open", [](const std::string& path, bool strict) { return Corsario(path, strict); },
        py::arg("path"), py::arg("strict") = true,
        "Open a CORSIKA .DAT file and return a Corsario object.");
}
