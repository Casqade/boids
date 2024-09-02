#pragma once

#if defined(_WIN32)
#include <windows.h>
using AffinityMaskType = DWORD_PTR;
using ProcessorCpuNumber = DWORD_PTR;
#else
#include <sched.h>
using AffinityMaskType = cpu_set_t;
using ProcessorCpuNumber = int;
#endif


void setThreadAffinity( const AffinityMaskType );

AffinityMaskType initAffinityMask();

void addCpuToAffinityMask( AffinityMaskType&, const size_t cpuId );

ProcessorCpuNumber getThreadProcessorNumber();
