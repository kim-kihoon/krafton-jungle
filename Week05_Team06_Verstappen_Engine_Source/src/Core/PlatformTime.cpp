#include "Core/PlatformTime.h"

namespace Core {
    double FWindowsPlatformTime::GSecondsPerCycle = 0.0;
    bool FWindowsPlatformTime::bInitialized = false;
}