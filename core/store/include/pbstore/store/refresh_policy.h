#pragma once
// When the panel gets a full refresh (research R-13, contracts/ui-pages.md): once at start, once when the kind of page changes
// (list, plan, progress, result), after every 10th page turn, and on leaving. Never for a repeated page of the same kind.
// Whether the values are right is the device check DS-10; the constants live here so that they can be tuned in one place.

namespace pbstore {
namespace store {

constexpr int kPageTurnsPerFullRefresh = 10;

enum class PageKind { List, Plan, Progress, Result, Other };

class RefreshPolicy {
public:
    bool start();                  // true the first time only
    bool show(PageKind kind);      // true when the kind of page changes (the first page after start is covered by start)
    bool page_turn();              // true on every 10th turn
    bool leave() { reset_turns(); return true; }

private:
    void reset_turns() { turns_ = 0; }
    bool started_ = false;
    bool have_kind_ = false;
    PageKind kind_ = PageKind::List;
    int turns_ = 0;
};

}  // namespace store
}  // namespace pbstore
