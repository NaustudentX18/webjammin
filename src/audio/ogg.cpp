#include "audio/ogg.hpp"

#include <cstring>

// Include stb_vorbis implementation directly
#include "audio/stb_vorbis.c"

namespace wj {

bool OggStream::open(const std::string& path, std::string& error) {
    close();
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_filename(path.c_str(), &err, nullptr);
    if (!v) {
        error = "cannot open ogg " + path + " (err " + std::to_string(err) + ")";
        return false;
    }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    channels_ = info.channels;
    sample_rate_ = info.sample_rate;
    duration_s_ = stb_vorbis_stream_length_in_seconds(v);
    vorbis_ = v;
    eof_ = false;
    error.clear();
    return true;
}

void OggStream::close() {
    if (vorbis_) {
        stb_vorbis_close(static_cast<stb_vorbis*>(vorbis_));
        vorbis_ = nullptr;
    }
    eof_ = false;
}

int OggStream::read(int16_t* out, int frames) {
    if (!vorbis_ || eof_) return 0;
    stb_vorbis* v = static_cast<stb_vorbis*>(vorbis_);
    if (channels_ == 2) {
        int n = stb_vorbis_get_samples_short_interleaved(v, 2, out, frames * 2);
        if (n <= 0) {
            eof_ = true;
            return 0;
        }
        return n;
    } else {
        std::vector<int16_t> mono(frames);
        int n = stb_vorbis_get_samples_short_interleaved(v, 1, mono.data(), frames);
        if (n <= 0) {
            eof_ = true;
            return 0;
        }
        for (int i = 0; i < n; ++i) {
            out[i * 2 + 0] = mono[i];
            out[i * 2 + 1] = mono[i];
        }
        return n;
    }
}

} // namespace wj
