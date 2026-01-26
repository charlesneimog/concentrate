// Class and objects
#include "focusservice.hpp"

// ─────────────────────────────────────
int main() {
    unsigned ServerPort = 7079;
    unsigned PingEach = 2; // Seconds to request AppID from Window
    FocusService FocusService(ServerPort, PingEach);
    return 0;
}
