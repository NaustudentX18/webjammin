// WeBjammin — streaming WAV reader.
//
// Songs are streamed from disk in small blocks: the Cardputer Zero has 512 MB
// of RAM and a 4-minute stereo track would be 46 MB decoded, which we refuse
// to hold. Only 16-bit PCM is supported (the format the generator writes and
// the one users can convert to with one ffmpeg line).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace wj {

class WavStream {
public:
    ~WavStream() { close(); }

    bool open(const std::string& path, std::string& error);
    void close();
    bool is_open() const { return file_ != nullptr; }
    bool eof() const { return eof_; }

    // Reads up to `frames` stereo frames, converting mono to stereo.
    // Returns the number of frames written; 0 means end of stream.
    int read(int16_t* out, int frames);

    double duration_seconds() const { return duration_s_; }
    int sample_rate() const { return sample_rate_; }

private:
    FILE* file_ = nullptr;
    int channels_ = 2;
    int sample_rate_ = 44100;
    int bits_ = 16;
    double duration_s_ = 0.0;
    bool eof_ = false;
    std::vector<int16_t> buffer_;   // interleaved source samples
    size_t buffer_pos_ = 0;
    size_t buffer_len_ = 0;
};

} // namespace wj
