#include "focus_rules.h"

#include <mutex>

namespace {
std::mutex rules_mutex;
FocusRules rules_state;
} // namespace

void set_focus_rules(FocusRules rules) {
    std::lock_guard<std::mutex> lock(rules_mutex);
    rules_state = std::move(rules);
}

FocusRules get_focus_rules() {
    std::lock_guard<std::mutex> lock(rules_mutex);
    return rules_state;
}