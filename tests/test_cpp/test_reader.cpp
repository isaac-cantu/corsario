// Minimal native C++ smoke test (no Python/pybind11 dependency), enabled via
// -DCORSARIO_BUILD_TESTS=ON. The full behavioral test matrix (format
// auto-detection, LONG blocks, truncation, CSV schema, exports) lives in
// tests/test_corsario.py, which is easier to extend and is what CI runs by
// default. This file only guards against breaking the public C++ API.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "corsario/corsario.hpp"

using corsario::Corsario;

namespace {

void write_f32(std::ofstream& f, float v) { f.write(reinterpret_cast<char*>(&v), 4); }
void write_tag(std::ofstream& f, const char* tag) { f.write(tag, 4); }

// Writes a minimal valid file: 1 marker word, RUNH, EVTH, one data block
// with 3 particles, EVTE, RUNE, 1 marker word. No super-block padding
// needed since we stay well under 21 sub-blocks.
std::string write_minimal_dat(const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    write_f32(f, 0.0f);  // leading marker

    write_tag(f, "RUNH");
    for (int i = 0; i < 272; ++i) write_f32(f, 0.0f);

    write_tag(f, "EVTH");
    float evth[272] = {0};
    evth[0] = 1;      // event_no
    evth[1] = 14;     // primary id
    evth[2] = 1e6f;   // total energy
    f.write(reinterpret_cast<char*>(evth), sizeof(evth));

    float data[273] = {0};
    // particle 1: id=6003
    data[0] = 6003; data[1] = 1.0f; data[2] = 2.0f; data[3] = 3.0f;
    data[4] = 10.0f; data[5] = 20.0f; data[6] = 30.0f;
    // particle 2
    data[7] = 6004; data[8] = 1.5f;
    // particle 3
    data[14] = 6005; data[15] = 2.5f;
    f.write(reinterpret_cast<char*>(data), sizeof(data));

    write_tag(f, "EVTE");
    for (int i = 0; i < 272; ++i) write_f32(f, 0.0f);

    write_tag(f, "RUNE");
    float rune[272] = {0};
    rune[1] = 1;  // n_events_processed
    f.write(reinterpret_cast<char*>(rune), sizeof(rune));

    write_f32(f, 0.0f);  // trailing marker
    f.close();
    return path;
}

}  // namespace

int main() {
    const std::string path = "/tmp/corsario_native_test.dat";
    write_minimal_dat(path);

    Corsario run(path);
    assert(run.n_showers() == 1);
    assert(run.total_particles() == 3);
    assert(run.format().has_markers == true);
    assert(run.format().thinning == false);

    const auto& sh = run.shower(0);
    assert(sh.n_particles() == 3);
    assert(sh.particle(0).particle_id() == 6003);
    assert(sh.particle(1).particle_id() == 6004);

    run.to_csv("/tmp/corsario_native_test.csv");
    std::ifstream csv("/tmp/corsario_native_test.csv");
    std::string header;
    std::getline(csv, header);
    assert(header == "particle_id,px,py,pz,x,y,t");

    std::remove(path.c_str());
    std::printf("test_reader: all assertions passed\n");
    return 0;
}
