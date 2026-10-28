#include "pbstore/store/refresh_policy.h"

namespace pbstore {
namespace store {

bool RefreshPolicy::start() {
    if (started_) return false;
    started_ = true;
    reset_turns();
    return true;
}

bool RefreshPolicy::show(PageKind kind) {
    if (!have_kind_) {
        have_kind_ = true;
        kind_ = kind;
        return false;
    }
    if (kind == kind_) return false;
    kind_ = kind;
    reset_turns();
    return true;
}

bool RefreshPolicy::page_turn() {
    if (++turns_ < kPageTurnsPerFullRefresh) return false;
    reset_turns();
    return true;
}

}  // namespace store
}  // namespace pbstore
