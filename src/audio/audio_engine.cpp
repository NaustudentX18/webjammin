#include "audio/audio_engine.hpp"

#include <alsa/asoundlib.h>
#include <cstring>

namespace wj {
namespace {
constexpr size_t kRingFrames = 48000 * 2; // 2 seconds of headroom at 48 kHz
}

bool AudioEngine::open(const std::string& device, int rate, int channels, int period_frames,
                        std::string& error) {
    close();
    snd_pcm_t* pcm = nullptr;
    const int err = snd_pcm_open(&pcm, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        error = "snd_pcm_open(" + device + "): " + snd_strerror(err);
        return false;
    }
    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_malloc(&params);
    snd_pcm_hw_params_any(pcm, params);
    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, params, channels);
    unsigned int r = static_cast<unsigned int>(rate);
    snd_pcm_hw_params_set_rate(pcm, params, r, 0);
    // A small period keeps the latency low; the ES8389 handles 256 frames
    // (5.3 ms) comfortably.
    snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(period_frames);
    snd_pcm_hw_params_set_period_size_near(pcm, params, &period, nullptr);
    snd_pcm_hw_params(pcm, params);
    snd_pcm_hw_params_free(params);

    // Small software buffer: we queue in the ring, so ALSA's own buffer can
    // stay tiny to keep the hit-to-sound delay short.
    snd_pcm_uframes_t buffer = static_cast<snd_pcm_uframes_t>(period_frames * 4);
    snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                       channels, static_cast<unsigned int>(rate), 1, buffer);

    pcm_ = pcm;
    rate_ = rate;
    channels_ = channels;
    period_ = period_frames;
    ring_frames_ = kRingFrames;
    ring_.assign(ring_frames_ * static_cast<size_t>(channels_), 0);
    ring_read_ = ring_write_ = 0;
    frames_written_.store(0);
    xruns_.store(0);
    running_.store(true);
    thread_ = std::thread([this] { writer_loop(); });
    open_ = true;
    error.clear();
    return true;
}

void AudioEngine::close() {
    if (!open_) {
        if (pcm_) {
            snd_pcm_close(static_cast<snd_pcm_t*>(pcm_));
            pcm_ = nullptr;
        }
        return;
    }
    running_.store(false);
    not_full_.notify_all();
    not_empty_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (pcm_) {
        snd_pcm_close(static_cast<snd_pcm_t*>(pcm_));
        pcm_ = nullptr;
    }
    open_ = false;
}

void AudioEngine::writer_loop() {
    snd_pcm_t* pcm = static_cast<snd_pcm_t*>(pcm_);
    std::vector<int16_t> block(static_cast<size_t>(period_) * static_cast<size_t>(channels_));
    while (running_.load(std::memory_order_relaxed)) {
        size_t avail = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (ring_read_ == ring_write_ && running_.load(std::memory_order_relaxed)) {
                not_empty_.wait_for(lock, std::chrono::milliseconds(50));
            }
            avail = (ring_write_ - ring_read_ + ring_frames_) % ring_frames_;
        }
        if (avail == 0) continue;

        const size_t n = std::min(avail, static_cast<size_t>(period_));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 0; i < n; ++i) {
                const size_t src = ((ring_read_ + i) % ring_frames_) * static_cast<size_t>(channels_);
                block[i * static_cast<size_t>(channels_)] = ring_[src];
                if (channels_ > 1) block[i * static_cast<size_t>(channels_) + 1] = ring_[src + 1];
            }
            ring_read_ = (ring_read_ + n) % ring_frames_;
        }
        not_full_.notify_all();

        const float vol = volume_.load(std::memory_order_relaxed);
        if (vol != 1.0f) {
            for (auto& s : block) s = static_cast<int16_t>(s * vol);
        }

        snd_pcm_sframes_t written = snd_pcm_writei(pcm, block.data(), static_cast<snd_pcm_uframes_t>(n));
        if (written < 0) {
            xruns_.fetch_add(1, std::memory_order_relaxed);
            if (snd_pcm_recover(pcm, static_cast<int>(written), 1) < 0) {
                // Unrecoverable: stop the writer, the game keeps running but
                // the clock stops advancing (reported in the diagnostics).
                break;
            }
            continue;
        }
        frames_written_.fetch_add(written, std::memory_order_relaxed);
    }
}

int AudioEngine::push(const int16_t* frames, int count) {
    if (!open_) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    size_t space = (ring_read_ + ring_frames_ - ring_write_) % ring_frames_;
    const size_t n = std::min<size_t>(static_cast<size_t>(count), space);
    for (size_t i = 0; i < n; ++i) {
        const size_t dst = ((ring_write_ + i) % ring_frames_) * static_cast<size_t>(channels_);
        ring_[dst] = frames[i * 2 + 0];
        if (channels_ > 1) ring_[dst + 1] = frames[i * 2 + 1];
    }
    ring_write_ = (ring_write_ + n) % ring_frames_;
    not_empty_.notify_all();
    return static_cast<int>(n);
}

long AudioEngine::played_frames() const {
    if (!open_) return 0;
    snd_pcm_t* pcm = static_cast<snd_pcm_t*>(pcm_);
    snd_pcm_sframes_t delay = 0;
    if (snd_pcm_delay(pcm, &delay) < 0) delay = 0;
    const long written = frames_written_.load(std::memory_order_relaxed);
    return written - static_cast<long>(delay);
}

long AudioEngine::queued_frames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<long>((ring_write_ - ring_read_ + ring_frames_) % ring_frames_);
}

void AudioEngine::set_volume(float v) {
    volume_.store(v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v), std::memory_order_relaxed);
}

void AudioEngine::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    ring_read_ = ring_write_;
}

bool AudioEngine::wait_for_space(int frames, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    const size_t need = static_cast<size_t>(frames);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (queued_frames() >= static_cast<long>(need) && running_.load(std::memory_order_relaxed)) {
        if (not_full_.wait_until(lock, deadline) == std::cv_status::timeout) {
            if (queued_frames() >= static_cast<long>(need)) return false;
        }
    }
    return true;
}

} // namespace wj
