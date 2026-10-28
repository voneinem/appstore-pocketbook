#include "pbstore/store/last_fetch.h"

namespace pbstore {
namespace store {

install::Downloader observe_fetches(install::Downloader inner, std::shared_ptr<LastFetch> record) {
    return [inner = std::move(inner), record = std::move(record)](const std::string& url, uint64_t max_bytes, long total_seconds, const install::DownloadProgress& progress) {
        FetchResult r = inner(url, max_bytes, total_seconds, progress);
        if (!r.ok && record) record->record(r.kind);
        return r;
    };
}

}  // namespace store
}  // namespace pbstore
