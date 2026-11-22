#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wj {

class OggStream {
public:
    ~OggStream() { close(); }

    bool open(const std::string& path, std::string& error);
    void close();
    bool is_open() const { return vorbis_ != nullptr; }
    bool eof() const { return eof_; }

    int read(int16_t* out, int frames);

    double duration_seconds() const { return duration_s_; }
    int sample_rate() const { return sample_rate_; }

private:
    void* vorbis_ = nullptr;
    int channels_ = 2;
    int sample_rate_ = 44100;
    double duration_s_ = 0.0;
    bool eof_ = false;
};

} // namespace wj
