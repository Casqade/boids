#pragma once

#include <chrono>

#if defined (PERFORMANCE_COUNTERS_ENABLED)
    #include <x86intrin.h>
#endif


template <typename TypeAbsolute, typename TypeRelative, typename TypeAverage>
struct PerformanceCounter
{
  using value_type = TypeAbsolute;
  using diff_type = TypeRelative;
  using printable_type = TypeAverage;

  struct
  {
    value_type begin {};
    value_type end {};

  } range {};

  diff_type elapsed {};

  printable_type average {};

  size_t hits {};


  PerformanceCounter() = default;

  inline value_type now() const;

  inline void begin()
  {
    ++hits;
    range.begin = now();
  }

  inline void begin( const PerformanceCounter& other )
  {
    ++hits;
    range.begin = other.range.begin;
  }

  inline void end()
  {
    range.end = now();
  }

  inline void end( const PerformanceCounter& other )
  {
    range.end = other.range.end;
  }

  inline bool update( const size_t steps )
  {
    elapsed += range.end - range.begin;
    range.begin = {};
    range.end = {};

    if ( hits % steps == 0 )
    {
      average = elapsed * 1.0 / steps;
      elapsed = {};

      if ( hits > 0 )
        return true;
    }

    return false;
  }
};


using TimePoint = std::chrono::time_point <
    std::chrono::high_resolution_clock, std::chrono::nanoseconds>;

using TimeDuration = TimePoint::duration;

using double_s  = std::chrono::duration <double>;
using double_ms = std::chrono::duration <double, std::milli>;
using double_us = std::chrono::duration <double, std::micro>;
using double_ns = std::chrono::duration <double, std::nano>;

using CyclePerfCounter = PerformanceCounter <
    size_t, size_t, size_t>;

using TimePerfCounter = PerformanceCounter <
    TimePoint, TimeDuration, double_us>;

template <>
inline CyclePerfCounter::value_type CyclePerfCounter::now() const
{
#if defined (PERFORMANCE_COUNTERS_ENABLED)
  return __rdtsc();
#endif
  return {};
}

template <>
inline TimePerfCounter::value_type TimePerfCounter::now() const
{
  return TimePoint::clock::now();
}


#if defined (PERFORMANCE_COUNTERS_ENABLED)

#define PERF_RESTART() \
  { timeCounter = {}; cycleCounter = {}; }

#define PERF_TIME_BEGIN(markerId) \
  { timeCounter[markerId].hits++; timeCounter[markerId].range.begin = timeCounter[markerId].now(); }

#define PERF_TIME_END(markerId) \
  { timeCounter[markerId].range.end = timeCounter[markerId].now(); }

#define PERF_TIME_BEGIN_COPY(target, source) \
  { timeCounter[target].hits++; timeCounter[target].range.begin = timeCounter[source].range.begin; }

#define PERF_TIME_END_COPY(target, source) \
  { timeCounter[target].range.end = timeCounter[source].range.end; }


#define PERF_CYCLES_BEGIN(markerId) \
  { cycleCounter[markerId].hits++; cycleCounter[markerId].range.begin = __rdtsc(); }

#define PERF_CYCLES_END(markerId) \
  { cycleCounter[markerId].range.end = __rdtsc(); }

#else

#define PERF_RESTART() {}
#define PERF_TIME_BEGIN(markerId) {}
#define PERF_TIME_END(markerId) {}
#define PERF_TIME_BEGIN_COPY(target, source)  {}
#define PERF_TIME_END_COPY(target, source) {}

#define PERF_CYCLES_BEGIN(markerId) {}
#define PERF_CYCLES_END(markerId) {}

#endif

#if defined(PERF_COUNTER_SHOW_EXAMPLE)

#include <iostream>

namespace
{
enum PerfMarker : size_t
{
  Marker0,
  Marker1,

  Count,
};

TimePerfCounter timeCounter [PerfMarker::Count] {};
CyclePerfCounter cycleCounter [PerfMarker::Count] {};
}

void repeatedly_called_func()
{
//  marker 0 measures the first half of the function
//  marker 1 measures the whole function

    PERF_TIME_BEGIN(PerfMarker::Marker0);

//    initialize marker1 from marker0
    PERF_TIME_BEGIN_COPY(PerfMarker::Marker1, PerfMarker::Marker0);

    PERF_CYCLES_BEGIN(PerfMarker::Marker1);
    PERF_CYCLES_BEGIN(PerfMarker::Marker0);

//    some heavy code segment

//    end marker0 measurement
    PERF_CYCLES_END(PerfMarker::Marker0);
    PERF_TIME_END(PerfMarker::Marker0);

//    another heavy code segment

//    end marker1 measurement
    PERF_CYCLES_END(PerfMarker::Marker1);
    PERF_TIME_END(PerfMarker::Marker1);

//    analyze the results
    for ( size_t i {}; i < PerfMarker::Count; ++i )
    {
        if ( cycleCounter[i].update(10) == true )
            std::cout << "marker " << i << " took " << cycleCounter[i].average << " cycles\n";

        if ( timeCounter[i].update(10) == true )
            std::cout << "marker " << i << " took " << timeCounter[i].average << " us\n";
    }
}

#endif
