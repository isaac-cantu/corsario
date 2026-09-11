#pragma once
// -----------------------------------------------------------------------------
// corsario/stream.hpp
//
// ShowerStream: the same single-pass block reader as Corsario, but
// resumable -- next_batch(N) parses forward and returns up to N complete
// showers, then remembers exactly where it left off (file position, format,
// open-shower state) for the next call.
//
// This exists for files with millions of showers, where holding every
// shower's full particle list in memory at once (what Corsario does) is
// not an option: a shower has ~7-8 floats per particle, and a file with,
// say, 15 million showers at even a modest few hundred particles each is
// already billions of particle records. Corsario is unchanged and remains
// the simple, eager, "just give me everything" API for files that
// comfortably fit in memory; it is now implemented as a thin wrapper
// around ShowerStream (see corsario.hpp) so both share exactly one parsing
// implementation. See docs/LARGE_FILES.md for the Python-side streaming
// API and typical memory budgeting.
// -----------------------------------------------------------------------------

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "corsario/format.hpp"
#include "corsario/particle.hpp"
#include "corsario/shower.hpp"

namespace corsario {

class ShowerStream {
public:
    explicit ShowerStream(const std::string& filename, bool strict = true)
        : filename_(filename), strict_(strict), file_(filename, std::ios::binary) {
        if (!file_) throw std::runtime_error("corsario: cannot open file: " + filename_);

        file_.seekg(0, std::ios::end);
        const long file_bytes = static_cast<long>(file_.tellg());
        file_.seekg(0, std::ios::beg);
        if (file_bytes < 4) {
            throw std::runtime_error("corsario: file is too small to be a CORSIKA DAT file: " + filename_);
        }

        format_ = detect_format(file_);
        total_words_ = file_bytes / 4;
        buf_.resize(static_cast<std::size_t>(format_.block_words));

        word_pos_ = format_.leading_offset;
        blocks_since_boundary_ = 0;
        shower_counter_ = 0;
        in_event_ = false;
        run_ended_ = false;
        eof_reached_ = false;
        current_ = Shower();
    }

    bool done() const { return run_ended_ || eof_reached_; }

    // Parses forward until `max_showers` complete showers have been
    // produced, or the run/file ends -- whichever comes first. May return
    // fewer than `max_showers` (including an empty vector) once done().
    std::vector<Shower> next_batch(int max_showers) {
        std::vector<Shower> batch;
        if (max_showers <= 0) return batch;
        while (!done() && static_cast<int>(batch.size()) < max_showers) {
            step(batch);
        }
        return batch;
    }

    const FormatInfo& format() const { return format_; }
    bool is_thinned() const { return format_.thinning; }
    bool has_markers() const { return format_.has_markers; }
    long run_number() const { return run_number_; }
    long n_events_processed() const { return n_events_processed_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    const std::string& filename() const { return filename_; }

private:
    std::string filename_;
    bool strict_;
    std::ifstream file_;
    FormatInfo format_;
    std::vector<float> buf_;

    long total_words_ = 0;
    long word_pos_ = 0;
    long blocks_since_boundary_ = 0;
    int shower_counter_ = 0;
    bool in_event_ = false;
    bool run_ended_ = false;
    bool eof_reached_ = false;
    Shower current_;

    long run_number_ = -1;
    long n_events_processed_ = -1;
    std::vector<std::string> warnings_;

    bool read_block(long pos) {
        file_.clear();
        file_.seekg(pos * 4, std::ios::beg);
        file_.read(reinterpret_cast<char*>(buf_.data()),
                   static_cast<std::streamsize>(format_.block_words) * 4);
        return file_.gcount() == static_cast<std::streamsize>(format_.block_words) * 4;
    }

    // Process exactly one sub-block; appends to `batch` iff it completes a
    // shower (EVTE). Mirrors Corsario::parse()'s per-block dispatch.
    void step(std::vector<Shower>& batch) {
        if (word_pos_ + format_.block_words > total_words_ || !read_block(word_pos_)) {
            finalize_incomplete(batch, "reached end of file");
            eof_reached_ = true;
            return;
        }

        char tag[4];
        std::memcpy(tag, buf_.data(), 4);

        if (std::memcmp(tag, "RUNH", 4) == 0) {
            run_number_ = static_cast<long>(buf_[1]);
        } else if (std::memcmp(tag, "EVTH", 4) == 0) {
            if (in_event_) {
                const std::string msg = "corsario: found EVTH before shower " +
                    std::to_string(current_.index()) + " was closed with EVTE";
                if (strict_) throw std::runtime_error(msg);
                warnings_.push_back(msg + " (kept partial shower)");
                batch.push_back(std::move(current_));
            }
            ++shower_counter_;
            current_ = Shower(shower_counter_);
            current_.event_no = buf_[1];
            current_.primary_id = buf_[2];
            current_.total_energy_gev = buf_[3];
            current_.starting_altitude_gcm2 = buf_[4];
            current_.first_target_no = buf_[5];
            current_.z_first_interaction_cm = buf_[6];
            current_.px_momentum = buf_[7];
            current_.py_momentum = buf_[8];
            current_.pz_momentum = buf_[9];
            current_.zenith_rad = buf_[10];
            current_.azimuth_rad = buf_[11];
            current_.thinned = format_.thinning;
            in_event_ = true;
        } else if (std::memcmp(tag, "LONG", 4) == 0) {
            if (in_event_) current_.has_longitudinal_blocks = true;
        } else if (std::memcmp(tag, "EVTE", 4) == 0) {
            if (in_event_) {
                current_.has_event_end = true;
                current_.n_photons_at_obs = buf_[2];
                current_.n_electrons_at_obs = buf_[3];
                current_.n_hadrons_at_obs = buf_[4];
                current_.n_muons_at_obs = buf_[5];
                current_.n_particles_written = buf_[6];
                batch.push_back(std::move(current_));
                current_ = Shower();
                in_event_ = false;
            } else {
                warnings_.push_back("corsario: EVTE found with no matching EVTH; ignored");
            }
        } else if (std::memcmp(tag, "RUNE", 4) == 0) {
            n_events_processed_ = static_cast<long>(buf_[2]);
            run_ended_ = true;
        } else if (in_event_) {
            const int words_per_particle = format_.words_per_particle;
            for (int slot = 0; slot < kParticlesPerDataBlock; ++slot) {
                const int base = slot * words_per_particle;
                if (base + words_per_particle > format_.block_words) break;
                const float description = buf_[base];
                if (description == 0.0f) break;
                const float weight = (words_per_particle == 8) ? buf_[base + 7] : 1.0f;
                current_.particles.emplace_back(
                    description, buf_[base + 1], buf_[base + 2], buf_[base + 3],
                    buf_[base + 4], buf_[base + 5], buf_[base + 6], weight);
            }
        }

        word_pos_ += format_.block_words;
        ++blocks_since_boundary_;
        if (blocks_since_boundary_ == kSubBlocksPerRecord) {
            blocks_since_boundary_ = 0;
            if (format_.has_markers) word_pos_ += kRecordMarkerWords;
        }
    }

    void finalize_incomplete(std::vector<Shower>& batch, const std::string& reason) {
        if (in_event_) {
            const std::string msg = "corsario: file ended while shower " +
                std::to_string(current_.index()) + " was still open (missing EVTE/RUNE): " + reason;
            if (strict_) throw std::runtime_error(msg);
            warnings_.push_back(msg);
            batch.push_back(std::move(current_));
            in_event_ = false;
        }
        if (!run_ended_) {
            warnings_.push_back("corsario: reached end of file without a RUNE tag "
                                 "(the run may have been truncated)");
        }
    }
};

}  // namespace corsario
