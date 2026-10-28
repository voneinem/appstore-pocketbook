#pragma once
// The battery as the store sees it. PowerGate is the only caller.

#include <optional>

namespace pbstore {
namespace store {

class Battery {
public:
    virtual ~Battery() = default;
    virtual std::optional<int> percent() = 0;        // 0..100, nullopt if it cannot be read
    virtual std::optional<bool> charging() = 0;      // nullopt if it cannot be read
};

}  // namespace store
}  // namespace pbstore
