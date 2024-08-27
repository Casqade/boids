#pragma once

#include "Containers.hpp"
#include "ThreadAffinity.hpp"

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

void
ThreadPool::init(
  AllocatorArena& allocator,
  const std::size_t threadCount,
  const std::size_t affinityOffset )
{
  threads = {allocator, threadCount};

  isRunning = true;

  for ( std::size_t i {}; i < threadCount; ++i )
    threads[i].thread = std::thread(
    [this, threadIndex = i, affinity = affinityOffset + i * 2] ()
    {
      auto mask = initAffinityMask();

      addCpuToAffinityMask(
        mask, affinity );

//      addCpuToAffinityMask(
//        mask, threadAffinity + 1 );

      setThreadAffinity(mask);

      while ( isRunning == true )
      {
        std::unique_lock lock {mut};

        newTaskReceived.wait( lock,
        [this]
        {
          return isRunning == false || pendingTask != nullptr;
        });

        if ( pendingTask == nullptr )
        {
          if ( isRunning == true )
            continue;

          return;
        }

        const auto task {std::move(pendingTask)};
        pendingTask = {};

        threads[threadIndex].isBusy = true;
        newTaskReceived.notify_all();
        lock.unlock();

        task(threadIndex);
        threads[threadIndex].isBusy = false;

        newTaskReceived.notify_all();
      }
    });
}

void
ThreadPool::deinit()
{
  {
    std::lock_guard lock {mut};
    isRunning = false;
  }

  newTaskReceived.notify_all();

  for ( std::size_t i {}; i < threads.length(); ++i )
  {
    if ( threads[i].thread.joinable() == true )
      threads[i].thread.join();

    threads[i].isBusy = false;
  }
}

void
ThreadPool::push(
  TaskPrototype&& task )
{
  std::unique_lock lock {mut};

  newTaskReceived.wait( lock,
  [this]
  {
    return pendingTask == nullptr;
  });

  pendingTask = std::move(task);

  newTaskReceived.notify_one();
  lock.unlock();
}

void
ThreadPool::push(
  std::function <void()>&& task )
{
  push(
  [task] ( const std::size_t threadId )
  {
    task();
  });
}

void
ThreadPool::parallel_for(
  ParallelForTaskPrototype&& task,
  const std::size_t iters,
  std::size_t threadCount )
{
  if ( threadCount == 0 )
    threadCount = threads.length();

  assert(threadCount > 0);

  const auto itersPerThread = iters / (threadCount + 1);

  for ( std::size_t threadId {}, rangeEnd {}; rangeEnd < iters; ++threadId )
  {
    const auto rangeStart = threadId * itersPerThread;

    rangeEnd = std::min(
      (threadId + 1) * itersPerThread,
      iters );

    if ( rangeEnd != iters )
      push(
      [task, rangeStart, rangeEnd] ( const std::size_t )
      {
        task(rangeStart, rangeEnd);
      });
    else
      task(rangeStart, rangeEnd);
  }
}

void
ThreadPool::waitForTasks()
{
  std::unique_lock lock (mut);

  newTaskReceived.wait( lock,
  [this]
  {
    return pendingTask == nullptr;
  });

  lock.unlock();

  for ( std::size_t i {}; i < threads.length(); ++i )
  {
    while ( threads[i].isBusy == true )
      ;
  }
}
