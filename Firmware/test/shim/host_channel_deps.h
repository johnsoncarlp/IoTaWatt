// Host test stand-ins for iotaInputChannel.cpp: the channel table (host_script_deps.h), the line
// frequency and the PCB version it reads. Shared by every suite that builds the real input channels.
#pragma once
#include "host_script_deps.h"
inline float   frequency = 60;
inline uint8_t deviceMajorVersion = 5;
