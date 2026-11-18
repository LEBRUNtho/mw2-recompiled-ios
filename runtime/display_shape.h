#pragma once
// The shape of screen the title is told it has. MW2 lays out its view and its
// HUD for a 4:3 television when the console says the display is not
// widescreen, still drawing into its 1280x720 buffer, which the console's
// scaler then stretched to the set. An iPad's screen is 4:3 (the 11-inch's a
// little wider), so there the title is told so and the frame fills the
// screen, where a 16:9 picture left a third of it black.
//
// A phone's screen is wider than 16:9, so there the title keeps its
// widescreen picture, which is shown whole between two bars (or zoomed, or
// stretched: MW2_FILL).
//
// MW2_ASPECT=4:3 or 16:9 (R); unset, 4:3 on an iPad and 16:9 elsewhere.
#include <cstring>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IOS
#include <sys/sysctl.h>
#include <cstdlib>
#endif
#include "env.h"

namespace display
{
    // An iPhone, by the machine's own name ("iPhone18,2"); the simulator's is
    // in its environment.
    inline bool Phone()
    {
#if defined(__APPLE__) && TARGET_OS_IOS
        static const bool phone = [] {
            char machine[64] = {};
            size_t size = sizeof machine - 1;
            if (const char* simulated = std::getenv("SIMULATOR_MODEL_IDENTIFIER"))
                return std::strncmp(simulated, "iPhone", 6) == 0 || std::strncmp(simulated, "iPod", 4) == 0;
            if (sysctlbyname("hw.machine", machine, &size, nullptr, 0) != 0) return false;
            return std::strncmp(machine, "iPhone", 6) == 0 || std::strncmp(machine, "iPod", 4) == 0;
        }();
        return phone;
#else
        return false;
#endif
    }

    inline bool FourByThree()
    {
        static const bool narrow = [] {
            if (const char* aspect = env::Text("MW2_ASPECT"))
                return std::strcmp(aspect, "4:3") == 0;
#if defined(__APPLE__) && TARGET_OS_IOS
            return !Phone();
#else
            return false;
#endif
        }();
        return narrow;
    }
}
