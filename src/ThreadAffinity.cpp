#include "ThreadAffinity.hpp"

#if !defined(_WIN32)
#include <pthread.h>
#endif

#include <cassert>


void
setThreadAffinity(
  const AffinityMaskType mask )
{
#if defined(_WIN32)
  SetThreadAffinityMask(
    GetCurrentThread(), mask );
#else
  const auto rc = pthread_setaffinity_np(
    pthread_self(), sizeof(mask), &mask );

  assert(rc == 0);
#endif
}

AffinityMaskType
initAffinityMask()
{
  AffinityMaskType mask {};

#if defined(_WIN32)

#else
  CPU_ZERO(&mask);
#endif

  return mask;
}

void
addCpuToAffinityMask(
  AffinityMaskType& mask,
  const size_t cpuId )
{
#if defined(_WIN32)
  mask |= 1 << cpuId;
#else
  CPU_SET(cpuId, &mask);
#endif
}

ProcessorCpuNumber
getThreadProcessorNumber()
{
#if defined(_WIN32)
  return GetCurrentProcessorNumber();
#else
  return sched_getcpu();
#endif
}
