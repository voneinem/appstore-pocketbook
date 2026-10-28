#pragma once
// The panel as the store sees it: only the request for a full refresh. RefreshPolicy decides when to ask.

namespace pbstore {
namespace store {

class Screen {
public:
    virtual ~Screen() = default;
    virtual void full_update() = 0;
};

}  // namespace store
}  // namespace pbstore
