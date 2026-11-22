#include "audio/wav.hpp"

#include <cstring>

namespace wj {
namespace {

uint32_t rd_u32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t rd_u16(const unsigned char* p) {
    return static_cast<uint16_t>(static_cast<uint32_t>(p[0]) |
                                 (static_cast<uint32_t>(p[1]) << 8));
}

} // namespace

bool WavStream::open(const std::string& path, std::string& error) {
    close();
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) {
        error = "cannot open wav: " + path;
        return false;
    }
    unsigned char hdr[12];
    if (std::fread(hdr, 1, 12, file_) != 12 || std::memcmp(hdr, "RIFF", 4) != 0 ||
        std::memcmp(hdr + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file: " + path;
        close();
        return false;
    }
    bool have_fmt = false;
    unsigned char chunk[8];
    while (std::fread(chunk, 1, 8, file_) == 8) {
        const uint32_t size = rd_u32(chunk + 4);
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            std::vector<unsigned char> fmt(size);
            if (size < 16 || std::fread(fmt.data(), 1, size, file_) != size) {
                error = "truncated fmt chunk";
                close();
                return false;
            }
            channels_ = rd_u16(fmt.data() + 2);
            sample_rate_ = static_cast<int>(rd_u32(fmt.data() + 4));
            bits_ = rd_u16(fmt.data() + 14);
            have_fmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            if (!have_fmt) {
                error = "data chunk before fmt";
                close();
                return false;
            }
            if (bits_ != 16) {
                error = "only 16-bit PCM wav is supported (got " + std::to_string(bits_) +
                        "-bit)";
                close();
                return false;
            }
            const double frames = static_cast<double>(size) / (channels_ * 2);
            duration_s_ = sample_rate_ > 0 ? frames / sample_rate_ : 0.0;
            buffer_.resize(4096 * static_cast<size_t>(channels_));
            return true;
        } else {
            std::fseek(file_, static_cast<long>(size) + (size & 1), SEEK_CUR);
            continue;
        }
    }
    error = "no data chunk in " + path;
    close();
    return false;
}

void WavStream::close() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    eof_ = false;
    buffer_pos_ = buffer_len_ = 0;
}

int WavStream::read(int16_t* out, int frames) {
    if (!file_ || eof_) return 0;
    int written = 0;
    while (written < frames) {
        if (buffer_pos_ >= buffer_len_) {
            const size_t want = buffer_.size();
            const size_t got = std::fread(buffer_.data(), 2, want, file_);
            if (got == 0) {
                eof_ = true;
                break;
            }
            buffer_len_ = got;
            buffer_pos_ = 0;
        }
        const int avail = static_cast<int>((buffer_len_ - buffer_pos_) / static_cast<size_t>(channels_));
        const int n = std::min(frames - written, avail);
        for (int i = 0; i < n; ++i) {
            int16_t l = 0, r = 0;
            const size_t base = buffer_pos_ + static_cast<size_t>(i) * static_cast<size_t>(channels_);
            l = buffer_[base];
            if (channels_ >= 2) r = buffer_[base + 1];
            else r = l;
            out[(written + i) * 2 + 0] = l;
            out[(written + i) * 2 + 1] = r;
        }
        buffer_pos_ += static_cast<size_t>(n) * static_cast<size_t>(channels_);
        written += n;
    }
    return written;
}

} // namespace wj
