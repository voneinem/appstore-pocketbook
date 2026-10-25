#include "signals.h"

#include <signal.h>

namespace pbstore {
namespace install {
namespace cli {

namespace {
CancelToken* g_token = nullptr;
struct sigaction g_previous;
void on_sigint(int) {
    if (g_token) g_token->cancel();
}
}  // namespace

SigintCancel::SigintCancel(CancelToken* token) {
    g_token = token;
    struct sigaction sa;
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, &g_previous);
}

SigintCancel::~SigintCancel() {
    sigaction(SIGINT, &g_previous, nullptr);
    g_token = nullptr;
}

}  // namespace cli
}  // namespace install
}  // namespace pbstore
