#pragma once
// -----------------------------------------------------------------------------
// corsario/format.hpp
//
// CORSIKA ".DAT" particle-output files are written by Fortran as sequential
// unformatted records. Two things vary between installations and are NOT
// announced anywhere inside the file itself:
//
//   1. Whether the compiler/runtime wraps every physical record (21 blocks
//      of 273 or 312 words) with 4-byte record-length markers. This is the
//      default for gfortran/g77 on Linux, but is absent for files written
//      with plain "stream" access, and the marker size/placement can differ
//      between platforms (CORSIKA_GUIDE, Sect. 10.2.2, footnote 110).
//   2. Whether the THIN option was enabled at simulation time, which changes
//      the sub-block size from 273 to 312 words and the particle record size
//      from 7 to 8 words (an extra "weight", CORSIKA_GUIDE Sect. 10.2.3).
//
// The previous corsario implementation hard-coded one specific combination
// (markers present, no thinning). That is the actual root cause behind the
// "reader breaks after some number of particles" symptom: the moment a file
// deviates from that one assumption (different CORSIKA/compiler build, or a
// THIN run — both very common for real cosmic-ray simulations), the fixed
// step of 273 words silently desynchronizes from the true record boundaries.
// For small showers the desync may not yet have crossed a super-block
// boundary, so a few events parse "by accident"; larger showers cross more
// boundaries and the reader drifts into garbage, corrupts the particle
// stream, or throws once it indexes past the end of the file.
//
// detect_format() removes the guesswork: it tries the four structurally
// possible combinations against the very first "RUNH" tag and keeps the one
// that matches, so the same corsario build reads files from any of the
// common CORSIKA configurations without recompiling or passing flags.
// -----------------------------------------------------------------------------

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace corsario {

constexpr int kParticlesPerDataBlock = 39;
constexpr int kSubBlocksPerRecord = 21;   // Fortran record = 21 sub-blocks
constexpr int kRecordMarkerWords = 2;     // trailing+leading marker between records

struct FormatInfo {
    bool has_markers = false;      // Fortran record-length markers present
    bool thinning = false;         // THIN option (8-word particle records)
    int block_words = 273;         // words per sub-block (273 or 312)
    int words_per_particle = 7;    // 7 or 8
    long leading_offset = 0;       // word index of the first "RUNH" tag (0 or 1)
};

namespace detail {

inline std::string read_tag_at(std::ifstream& file, long word_index) {
    file.clear();
    file.seekg(word_index * 4, std::ios::beg);
    char buf[4] = {0, 0, 0, 0};
    file.read(buf, 4);
    if (file.gcount() != 4) return std::string();
    return std::string(buf, 4);
}

}  // namespace detail

// Inspect the beginning of the file and determine which of the four
// (markers x thinning) combinations produces a valid "RUNH" tag followed,
// at the expected offset, by the next sub-block's tag. Real CORSIKA runs
// always have at least one shower, so the block right after RUN HEADER is
// EVENT HEADER ('EVTH') -- checking for it is what lets us tell a 273-word
// (no thinning) block apart from a 312-word (THIN option) block, since the
// leading tag alone ("RUNH") looks identical in both cases.
inline FormatInfo detect_format(std::ifstream& file) {
    struct Candidate {
        bool markers;
        bool thin;
    };
    static constexpr Candidate candidates[4] = {
        {false, false},
        {true, false},
        {false, true},
        {true, true},
    };

    bool found_runh_only = false;
    for (const auto& c : candidates) {
        const long offset = c.markers ? 1 : 0;
        const int block_words = c.thin ? 312 : 273;
        if (detail::read_tag_at(file, offset) != "RUNH") continue;
        found_runh_only = true;
        const std::string next_tag = detail::read_tag_at(file, offset + block_words);
        if (next_tag == "EVTH" || next_tag == "RUNE") {
            FormatInfo info;
            info.has_markers = c.markers;
            info.thinning = c.thin;
            info.block_words = block_words;
            info.words_per_particle = c.thin ? 8 : 7;
            info.leading_offset = offset;
            return info;
        }
    }

    if (found_runh_only) {
        throw std::runtime_error(
            "corsario: found a 'RUNH' tag but could not confirm the block "
            "size (273 vs. 312 words) from the following sub-block. The "
            "file may be truncated or use an unsupported output variant.");
    }
    throw std::runtime_error(
        "corsario: could not detect the CORSIKA DAT layout (no 'RUNH' tag "
        "found at any of the expected offsets). The file may be truncated, "
        "corrupted, or written with an unsupported variant such as the "
        "COMPACT particle output option.");
}

}  // namespace corsario
