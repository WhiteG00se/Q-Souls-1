#pragma once
#include <cstdint>
// Q-Souls addition: the scaled clock must not jump when the cap changes.
struct LiveFpsClock {
    int64_t real_anchor = 0, virtual_anchor = 0, game_anchor = -1;
    unsigned rate = 60;
    int64_t sample(int64_t real, int64_t anchor, unsigned target) {
        if (anchor != game_anchor) {
            real_anchor = virtual_anchor = real; game_anchor = anchor; rate = target;
        }
        if (target != rate) {
            virtual_anchor += (real - real_anchor) * rate / 60;
            real_anchor = real; rate = target;
        }
        return virtual_anchor + (real - real_anchor) * target / 60;
    }
};
