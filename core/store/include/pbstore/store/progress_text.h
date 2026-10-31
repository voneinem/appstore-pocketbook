#pragma once
// What the progress line of an operation says and how often it is redrawn (research R-9, spec FR-017). The engine reports raw numbers; people
// read "3.1 of 7.4 MB" and "file 12 of 80". The redraw is held back to one per kProgressMinIntervalMs because every redraw of the panel is
// visible on e-ink; the time is passed in so that a test needs no clock.

#include <atomic>
#include <cstdint>
#include <string>

#include "pbstore/install/progress.h"

namespace pbstore {
namespace store {

constexpr int64_t kProgressMinIntervalMs = 2000;

// The figure in brackets: "3.1 of 7.4 MB", "file 12 of 80", "step 3 of 7"; with no total "3.1 MB" or "12 files"; "" when nothing is measurable.
std::string progress_figure(const install::Progress& p);

// "Downloading the package (3.1 of 7.4 MB)"
std::string progress_line(const install::Progress& p);

class ProgressThrottle {
public:
    explicit ProgressThrottle(int64_t min_interval_ms = kProgressMinIntervalMs) : min_(min_interval_ms) {}
    // True when a line may be drawn at `now_ms` (and counts it as drawn).
    bool pass(int64_t now_ms) {
        const int64_t last = last_.load();
        if (last != kNever && now_ms - last < min_) return false;
        last_.store(now_ms);
        return true;
    }
    void reset() { last_.store(kNever); }   // each phase starts with a report that is not held back by the one before it

private:
    static constexpr int64_t kNever = INT64_MIN;
    int64_t min_;
    std::atomic<int64_t> last_{kNever};
};

}  // namespace store
}  // namespace pbstore
