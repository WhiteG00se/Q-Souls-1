#ifndef QSOULS_RESPEC_MODEL_H
#define QSOULS_RESPEC_MODEL_H

#include <stdio.h>
#include <string.h>

enum { RESPEC_STATS = 8, RESPEC_MIN = 10, RESPEC_MAX = 99 };
enum { RESPEC_REQUEST = 70000980, RESPEC_DONE, RESPEC_COMMITTED, RESPEC_READY };
typedef struct {
    int original[RESPEC_STATS], values[RESPEC_STATS];
    int level, selected, freePoints;
} RespecDraft;

static inline int respecBegin(RespecDraft *draft, const int *base, int level)
{
    if (level < 1) return 0;
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
    if (direction < 0 && *value > RESPEC_MIN) { --*value; ++draft->freePoints; return 1; }
    if (direction > 0 && draft->freePoints > 0 && *value < RESPEC_MAX) {
        ++*value; --draft->freePoints; return 1;
    }
    return 0;
}
static inline int respecCanCommit(const RespecDraft *draft, const int *base, int level)
{
    int sum = 0;
    if (draft->freePoints || draft->level != level || memcmp(draft->original, base, sizeof(draft->original))) return 0;
    for (int i = 0; i < RESPEC_STATS; ++i) {
        if (draft->values[i] < RESPEC_MIN || draft->values[i] > RESPEC_MAX) return 0;
        sum += draft->values[i] - draft->original[i];
    }
    return sum == 0;
}
static inline void respecText(const RespecDraft *draft, const char *message, char *text, size_t size)
{
    static const char *names[] = {"Vitality", "Attunement", "Endurance", "Strength", "Dexterity", "Resistance", "Intelligence", "Faith"};
    size_t used = 0;
    int count = snprintf(text, size, "Respec\n\n");
    if (count < 0 || (size_t)count >= size) return;
    used = (size_t)count;
    for (int i = 0; i < RESPEC_STATS; ++i) {
        count = snprintf(text + used, size - used, i == draft->selected ? "%s < %d >\n" : "%s %d\n", names[i], draft->values[i]);
        if (count < 0 || (size_t)count >= size - used) return;
        used += (size_t)count;
    }
    snprintf(text + used, size - used, "\nSoul Level: %d\nFree Points: %d\n\n%s", draft->level, draft->freePoints,
             message && *message ? message : "Arrows / D-pad: adjust    Back: apply and return");
}
#endif
