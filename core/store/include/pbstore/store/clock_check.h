#pragma once
// The guess "the reader's clock looks wrong" (research R-10, spec Edge Cases). It is made only after a TLS failure, only
// changes the wording of a message (E-STO-04 instead of the certificate message), and is never an input to a check: the
// signature check, the serial comparison and the offer rule do not use the clock at all, and no setting can skip, weaken or
// postpone the certificate check.

#include <optional>
#include <string>

#include "pbstore/fetch.h"
#include "pbstore/install/clock.h"

namespace pbstore {
namespace store {

// True when the transfer failed with FetchKind::Tls and the clock is before the later of the release date of this store
// ("YYYY-MM-DD", may be empty) and the generation date of the cached catalog ("YYYY-MM-DDTHH:MM:SSZ"), or the year is below
// 2024 or above 2100.
bool clock_looks_wrong(const install::Utc& now, const std::string& release_date, const std::optional<std::string>& cached_generated_at, FetchKind failure);

// The date the reader believes, as YYYY-MM-DD (UTC).
std::string clock_date_text(const install::Utc& now);

}  // namespace store
}  // namespace pbstore
