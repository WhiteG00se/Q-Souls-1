#ifndef QSOULS_RESPEC_MODEL_H
#define QSOULS_RESPEC_MODEL_H

#include <stdio.h>
#include <string.h>
#include <limits.h>

enum { RESPEC_STATS = 8, RESPEC_MIN = 10, RESPEC_MAX = 99 };
typedef struct {
    int original[RESPEC_STATS], values[RESPEC_STATS];
    int level, selected, freePoints;
    int purchased, souls, cost;
    int (*price)(int);
} RespecDraft;

static inline int respecBegin(RespecDraft *draft, const int *base, int level)
{
    if (level < 1 || level > 713) return 0;
    for (int i = 0; i < RESPEC_STATS; ++i)
        if (base[i] < RESPEC_MIN || base[i] > RESPEC_MAX) return 0;
    memset(draft, 0, sizeof(*draft));
    memcpy(draft->original, base, sizeof(draft->original));
    memcpy(draft->values, base, sizeof(draft->values));
    draft->level = level;
    return 1;
}
static inline void respecMove(RespecDraft *draft, int direction)
{
    draft->selected = (draft->selected + (direction < 0 ? RESPEC_STATS - 1 : 1)) % RESPEC_STATS;
}
static inline int respecAdjust(RespecDraft *draft, int direction)
{
    int *value = &draft->values[draft->selected];
    if ((direction < 0 && *value <= RESPEC_MIN) || (direction > 0 && *value >= RESPEC_MAX)) return 0;
    int delta = direction < 0 ? -1 : 1;
    int net = draft->purchased - draft->freePoints + delta;
    int purchased = net > 0 ? net : 0, cost = 0;
    if (draft->level > 713 - purchased) return 0;
    for (int i = 1; i <= purchased; ++i) {
        int price = draft->price ? draft->price(draft->level + i) : 0;
        if (price <= 0 || cost > INT_MAX - price) return 0;
        cost += price;
    }
    if (cost > draft->souls) return 0;
    *value += delta; draft->purchased = purchased; draft->freePoints = net < 0 ? -net : 0; draft->cost = cost;
    return 1;
}

static inline int respecCanCommit(const RespecDraft *draft, const int *base, int level)
{
    int sum = 0;
    if (draft->freePoints || draft->level != level || memcmp(draft->original, base, sizeof(draft->original))) return 0;
    for (int i = 0; i < RESPEC_STATS; ++i) {
        if (draft->values[i] < RESPEC_MIN || draft->values[i] > RESPEC_MAX) return 0;
        sum += draft->values[i] - draft->original[i];
    }
    if (sum != draft->purchased || draft->purchased < 0 || level > 713 - draft->purchased) return 0;
    int cost = 0;
    for (int i = 1; i <= draft->purchased; ++i) {
        int price = draft->price ? draft->price(level + i) : 0;
        if (price <= 0 || cost > INT_MAX - price) return 0;
        cost += price;
    }
    return cost == draft->cost && cost <= draft->souls;
}
#endif
