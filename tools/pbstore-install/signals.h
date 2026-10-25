#pragma once
// Ctrl-C is a cancel with the rules of spec FR-037.

#include "pbstore/install/progress.h"

namespace pbstore {
namespace install {
namespace cli {

// While alive, SIGINT cancels the token instead of ending the program.
class SigintCancel {
public:
    explicit SigintCancel(CancelToken* token);
    ~SigintCancel();
    SigintCancel(const SigintCancel&) = delete;
    SigintCancel& operator=(const SigintCancel&) = delete;
};

}  // namespace cli
}  // namespace install
}  // namespace pbstore
