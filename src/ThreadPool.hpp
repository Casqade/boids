#pragma once

#include "Containers.hpp"

#include <mutex>
#include <thread>
#include <functional>
#include <condition_variable>


struct ThreadPool
{
  struct ThreadEntry
  {
    volatile bool isBusy {};
    std::thread thread {};
  };

  Array <ThreadEntry> threads {};

  bool isRunning {};

  mutable std::mutex mut {};
  std::condition_variable newTaskReceived {};

  using TaskPrototype =
    std::function <void( const std::size_t threadId )>;

  using ParallelForTaskPrototype =
    std::function <void( const std::size_t rangeStart, const std::size_t rangeEnd )>;

  TaskPrototype pendingTask {};


  void init(
    AllocatorArena&,
    const std::size_t threadCount = {},
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
