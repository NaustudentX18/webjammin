// WeBjammin — ALSA output engine.
//
// Owns the sound card, a lock-free-ish ring buffer and a writer thread. The
// playback position (frames written minus the frames still in the device) is
// the game's master clock: notes are scheduled against it, so audio and
// visuals cannot drift over a four-minute song.
//
// On the Cardputer Zero the device is `plughw:CARD=ES8389Audio,DEV=0`.
// `pcm.!default` must NOT be used: it is PipeWire's, and PipeWire holds the
// codec open.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace wj {

class AudioEngine {
public:
    ~AudioEngine() { close(); }

    bool open(const std::string& device, int rate, int channels, int period_frames,
              std::string& error);
    void close();
    bool is_open() const { return open_; }

    // Queues interleaved stereo frames. Returns how many frames were accepted
    // (less than requested when the ring buffer is full).
    int push(const int16_t* frames, int count);

    // Master clock: frames actually handed to the sound card.
    long played_frames() const;
    long queued_frames() const;
    long xruns() const { return xruns_.load(std::memory_order_relaxed); }

    void set_volume(float v);
    float volume() const { return volume_.load(std::memory_order_relaxed); }

    // Drops everything queued (used when the song is stopped or seeked).
    void flush();

    // Blocks until at least `frames` are queued or the timeout expires.
    // Returns false on timeout.
    bool wait_for_space(int frames, int timeout_ms);

    int sample_rate() const { return rate_; }
    int channels() const { return channels_; }

private:
    void writer_loop();

    bool open_ = false;
    int rate_ = 48000;
    int channels_ = 2;
    int period_ = 256;
    void* pcm_ = nullptr; // snd_pcm_t*, kept opaque to avoid the header here

    std::vector<int16_t> ring_;
    size_t ring_frames_ = 0;
    size_t ring_read_ = 0;
    size_t ring_write_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<long> frames_written_{0};
    std::atomic<long> xruns_{0};
    std::atomic<float> volume_{1.0f};
};

} // namespace wj
