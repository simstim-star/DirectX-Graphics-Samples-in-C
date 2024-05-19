#pragma once
#include <windows.h>
#include <basetsd.h>
#include <stdbool.h>

// Integer format represents time using 10,000,000 ticks per second.
#define TicksPerSecond 10000000

inline double TicksToSeconds(UINT64 ticks) { return ((double)ticks) / TicksPerSecond; }
inline UINT64 SecondsToTicks(double seconds) { return (UINT64)(seconds * TicksPerSecond); }

// Helper class for animation and simulation timing.
typedef struct StepTimer
{
    // Source timing data uses QueryPerformanceCounter units.
    LARGE_INTEGER qpcFrequency;
    LARGE_INTEGER qpcLastTime;
    UINT64 qpcMaxDelta;
    // Derived timing data uses a canonical tick format.
    UINT64 elapsedTicks;
    UINT64 totalTicks;
    UINT64 leftOverTicks;
    // Members for tracking the framerate.
    UINT32 frameCount;
    UINT32 framesPerSecond;
    UINT32 framesThisSecond;
    UINT64 qpcSecondCounter;
    // Members for configuring fixed timestep mode.
    bool isFixedTimeStep;
    UINT64 targetElapsedTicks;
} StepTimer;

inline void StepTimer_Init(StepTimer *st) {
    st->elapsedTicks = 0;
    st->totalTicks = 0;
    st->leftOverTicks = 0;
    st->frameCount = 0;
    st->framesPerSecond = 0;
    st->framesThisSecond = 0;
    st->qpcSecondCounter = 0;
    st->isFixedTimeStep = false;
    st->targetElapsedTicks = TicksPerSecond / 60;
    
    QueryPerformanceFrequency(&st->qpcFrequency);
    QueryPerformanceCounter(&st->qpcLastTime);

    // Initialize max delta to 1/10 of a second.
    st->qpcMaxDelta = st->qpcFrequency.QuadPart / 10;
}

// After an intentional timing discontinuity (for instance a blocking IO operation)
// call this to avoid having the fixed timestep logic attempt a set of catch-up 
// Update calls.
inline void ResetElapsedTime(StepTimer* st)
{
    QueryPerformanceCounter(&st->qpcLastTime);

    st->leftOverTicks = 0;
    st->framesPerSecond = 0;
    st->framesThisSecond = 0;
    st->qpcSecondCounter = 0;
}

typedef void(*LPUPDATEFUNC) (void);

// Update timer state, calling the specified Update function the appropriate number of times.
inline void TickWithUpdateFn(StepTimer* st, LPUPDATEFUNC update)
{
    // Query the current time.
    LARGE_INTEGER currentTime;

    QueryPerformanceCounter(&currentTime);

    UINT64 timeDelta = currentTime.QuadPart - st->qpcLastTime.QuadPart;

    st->qpcLastTime = currentTime;
    st->qpcSecondCounter += timeDelta;

    // Clamp excessively large time deltas (e.g. after paused in the debugger).
    if (timeDelta > st->qpcMaxDelta)
    {
        timeDelta = st->qpcMaxDelta;
    }

    // Convert QPC units into a canonical tick format. This cannot overflow due to the previous clamp.
    timeDelta *= TicksPerSecond;
    timeDelta /= st->qpcFrequency.QuadPart;

    UINT32 lastFrameCount = st->frameCount;

    if (st->isFixedTimeStep)
    {
        // Fixed timestep update logic

        // If the app is running very close to the target elapsed time (within 1/4 of a millisecond) just clamp
        // the clock to exactly match the target value. This prevents tiny and irrelevant errors
        // from accumulating over time. Without this clamping, a game that requested a 60 fps
        // fixed update, running with vsync enabled on a 59.94 NTSC display, would eventually
        // accumulate enough tiny errors that it would drop a frame. It is better to just round 
        // small deviations down to zero to leave things running smoothly.

        if (abs((int)(timeDelta - st->targetElapsedTicks)) < TicksPerSecond / 4000)
        {
            timeDelta = st->targetElapsedTicks;
        }

        st->leftOverTicks += timeDelta;

        while (st->leftOverTicks >= st->targetElapsedTicks)
        {
            st->elapsedTicks = st->targetElapsedTicks;
            st->totalTicks += st->targetElapsedTicks;
            st->leftOverTicks -= st->targetElapsedTicks;
            st->frameCount++;

            if (update)
            {
                update();
            }
        }
    }
    else
    {
        // Variable timestep update logic.
        st->elapsedTicks = timeDelta;
        st->totalTicks += timeDelta;
        st->leftOverTicks = 0;
        st->frameCount++;

        if (update)
        {
            update();
        }
    }

    // Track the current framerate.
    if (st->frameCount != lastFrameCount)
    {
        st->framesThisSecond++;
    }

    if (st->qpcSecondCounter >= (UINT64)(st->qpcFrequency.QuadPart))
    {
        st->framesPerSecond = st->framesThisSecond;
        st->framesThisSecond = 0;
        st->qpcSecondCounter %= st->qpcFrequency.QuadPart;
    }
}

inline void Tick(StepTimer* st) { TickWithUpdateFn(st, NULL); }