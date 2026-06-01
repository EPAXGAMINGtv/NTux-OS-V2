#include <signal.h>

static sighandler_t g_handlers[64];

sighandler_t signal(int sig, sighandler_t handler) {
    if (sig < 0 || sig >= (int)(sizeof(g_handlers) / sizeof(g_handlers[0]))) {
        return SIG_ERR;
    }
    sighandler_t prev = g_handlers[sig];
    g_handlers[sig] = handler;
    return prev ? prev : SIG_DFL;
}

int raise(int sig) {
    if (sig < 0 || sig >= (int)(sizeof(g_handlers) / sizeof(g_handlers[0]))) {
        return -1;
    }
    sighandler_t h = g_handlers[sig];
    if (h && h != SIG_IGN && h != SIG_DFL) {
        h(sig);
    }
    return 0;
}
