#pragma once

#include "Containers.hpp"

#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <condition_variable>


struct ThreadPool
{
  struct ThreadEntry
  {
    std::thread thread {};
    std::atomic_bool isBusy {};
  };

  using TaskPrototype =
    std::function <void( const std::size_t threadId )>;

  using ParallelForTaskPrototype =
    std::function <void( const std::size_t rangeStart, const std::size_t rangeEnd )>;

  struct alignas(64) TaskStorage
  {
    TaskPrototype task {};
  };


private:
  Array <ThreadEntry> mThreads {};

  std::mutex mTasksAvailableMutex {};
  std::condition_variable mTasksAvailable {};

  std::atomic_bool mShutdownRequested {};

  RingBuffer <TaskStorage> mTasks {};


public:
  void init(
    AllocatorArena&,
    const std::size_t taskBufferSize,
    const std::size_t threadCount,
    const std::size_t threadAffinityOffset = size_t{2} );

  void deinit();

  void push( TaskPrototype&& );

  void push( std::function <void()>&& task );

  void parallel_for(
    ParallelForTaskPrototype&&,
    const std::size_t iters,
    std::size_t threadCount = {} );

  void waitForTasks();
};
