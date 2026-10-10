#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace mm::engine {

/// Hands the transport position from the audio thread to the editor for the playhead of the piano roll. The audio
/// thread publishes the position at the start of every block with the time of that moment; the editor asks for the
/// position "now" and gets it by running the tempo forward from the last block. No JUCE, no allocation, no locks: a
/// sequence counter makes the three values one reading (the writer is the audio thread only, the reader any other).
class PlayheadMeter {
public:
    /// How long a block's reading is extrapolated. Longer than this without a new block means the instance is not
    /// processing: no playhead.
    static constexpr double kMaxAgeSeconds = 0.25;

    /// Audio thread, once per block. `playing` false or no position: the playhead hides.
    void publish(bool playing, double ppq, double bpm, double nowSeconds) {
        const auto seq = seq_.load(std::memory_order_relaxed);
        seq_.store(seq + 1, std::memory_order_relaxed); // odd: a write is going on
        std::atomic_thread_fence(std::memory_order_release); // the odd counter is visible before the new values
        playing_.store(playing, std::memory_order_relaxed);
        ppq_.store(ppq, std::memory_order_relaxed);
        bpm_.store(bpm, std::memory_order_relaxed);
        stamp_.store(nowSeconds, std::memory_order_relaxed);
        seq_.store(seq + 2, std::memory_order_release);
    }

    /// The position in PPQ at `nowSeconds` (the same clock as `publish`), or none when nothing plays, the reading is
    /// older than `kMaxAgeSeconds`, or nothing was published yet. Never ahead of the reading by more than that age.
    std::optional<double> ppqAt(double nowSeconds) const {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const auto before = seq_.load(std::memory_order_acquire);
            if (before == 0 || (before & 1) != 0) {
                if (before == 0) {
                    return std::nullopt;
                }
                continue;
            }
            const bool playing = playing_.load(std::memory_order_relaxed);
            const double ppq = ppq_.load(std::memory_order_relaxed);
            const double bpm = bpm_.load(std::memory_order_relaxed);
            const double stamp = stamp_.load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire); // the values are read before the counter is checked
            if (seq_.load(std::memory_order_relaxed) != before) {
                continue;
            }
            const double age = nowSeconds - stamp;
            if (!playing || ppq < 0.0 || bpm <= 0.0 || age > kMaxAgeSeconds) {
                return std::nullopt;
            }
            return ppq + (age > 0.0 ? age : 0.0) * bpm / 60.0;
        }
        return std::nullopt;
    }

private:
    std::atomic<uint32_t> seq_{0};
    std::atomic<bool> playing_{false};
    std::atomic<double> ppq_{0.0};
    std::atomic<double> bpm_{0.0};
    std::atomic<double> stamp_{0.0};
};

} // namespace mm::engine
