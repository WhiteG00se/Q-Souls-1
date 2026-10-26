#pragma once
// Keep future quality controls alongside this first setting.
static const unsigned qualityDefaultFps = 120;
static const unsigned qualityFpsLimits[] = {30, 60, 120, 240};
static inline unsigned qualityNextFps(unsigned current, int direction)
{
    const unsigned count = sizeof(qualityFpsLimits) / sizeof(qualityFpsLimits[0]);
    for (unsigned i = 0; i < count; ++i) if (qualityFpsLimits[i] == current)
        return qualityFpsLimits[direction < 0 ? (i ? i - 1 : 0) : (i + 1 < count ? i + 1 : i)];
    return qualityDefaultFps;
}
static inline bool qualityValidFps(unsigned value)
{
    for (unsigned fps : qualityFpsLimits) if (fps == value) return true;
    return false;
}
