#pragma once
#include <stddef.h>
// Persistent quality controls; clamp at either end of each list.
static const unsigned qualityDefaultFps=60, qualityDefaultFov=48, qualityDefaultDistance=100;
static const unsigned qualityFpsLimits[]={30,60,120,240};
static const unsigned qualityFovDegrees[]={40,44,48,52,56,60,64,68};
static const unsigned qualityDistances[]={50,60,70,80,90,100,110,120,130,140,150,160,170,180,190,200,225,250,275,300};
template<size_t N> static inline unsigned qualityNext(const unsigned (&values)[N],unsigned current,int direction,unsigned fallback)
{
    for (size_t i=0;i<N;++i) if (values[i]==current)
        return values[direction<0 ? (i ? i-1 : 0) : (i+1<N ? i+1 : i)];
    return fallback;
}
template<size_t N> static inline bool qualityValid(const unsigned (&values)[N],unsigned value)
{
    for (unsigned item : values) if (item==value) return true;
    return false;
}
static inline unsigned qualityNextFps(unsigned current,int direction)
{
    return qualityNext(qualityFpsLimits,current,direction,qualityDefaultFps);
}
static inline bool qualityValidFps(unsigned value) { return qualityValid(qualityFpsLimits,value); }
static inline const char *qualityDefaultLabel(unsigned value,unsigned defaultValue)
{
    return value==defaultValue ? " (default)" : "";
}
