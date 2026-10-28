#pragma once
// What the last network step failed with. The engine's findings say it in words (E-INS-26 for any download problem); the store
// needs the kind to tell a certificate failure on a reader whose clock is wrong (E-STO-04) from other failures. The store wraps
// the engine's Downloader with observe_fetches() and clears the record at the start of each action.

#include <memory>
#include <mutex>
#include <optional>

#include "pbstore/fetch.h"
#include "pbstore/install/net.h"

namespace pbstore {
namespace store {

class LastFetch {
public:
    void clear() {
        std::lock_guard<std::mutex> l(mu_);
        failure_.reset();
    }
    void record(FetchKind kind) {
        std::lock_guard<std::mutex> l(mu_);
        failure_ = kind;
    }
    std::optional<FetchKind> failure() const {
        std::lock_guard<std::mutex> l(mu_);
        return failure_;
    }

private:
    mutable std::mutex mu_;
    std::optional<FetchKind> failure_;
};

// The same downloader, remembering the kind of the last failed transfer.
install::Downloader observe_fetches(install::Downloader inner, std::shared_ptr<LastFetch> record);

}  // namespace store
}  // namespace pbstore
