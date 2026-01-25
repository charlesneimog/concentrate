// Class and objects
#include "focusservice.hpp"

// ─────────────────────────────────────
int main(int argc, const char *argv[]) {
    unsigned ServerPort = 7079;
    unsigned PingEach = 2; // Seconds
    FocusService FocusService(ServerPort, PingEach);

    return 0;
}
