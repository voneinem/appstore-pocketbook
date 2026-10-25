#pragma once
// Progress reports and cancellation (spec FR-037). The engine never prints; front ends show these.

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace pbstore {
namespace install {

struct Progress {
    std::string phase;      // "downloading", "checking", "preparing", "applying", "finishing"
    uint64_t done = 0;
    uint64_t total = 0;     // 0: not measurable; the text then says what is going on
    std::string unit;       // "bytes", "files", "steps" or "" when there is nothing to measure
    std::string text;       // a short sentence for people
};
using ProgressFn = std::function<void(const Progress&)>;

// Polled by the engine between steps. Cancelling before the commit point undoes the operation.
class CancelToken {
public:
    void cancel() { flag_.store(true); }
    bool cancelled() const { return flag_.load(); }

private:
    std::atomic<bool> flag_{false};
};

}  // namespace install
}  // namespace pbstore
