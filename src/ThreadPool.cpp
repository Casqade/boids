#include "ThreadPool.hpp"
#include "ThreadAffinity.hpp"

#include <cassert>


void
ThreadPool::init(
  AllocatorArena& allocator,
  const std::size_t taskBufferSize,
  const std::size_t threadCount,
  const std::size_t affinityOffset )
{
  mThreads = {allocator, threadCount};

  mTasks.init(allocator, taskBufferSize);

  const auto workerTask =
  [this] ( const std::size_t threadId, const std::size_t affinity )
  {
    auto mask = initAffinityMask();

    addCpuToAffinityMask(
      mask, affinity );

//      addCpuToAffinityMask(
//        mask, threadAffinity + 1 );

    setThreadAffinity(mask);


    while ( true )
    {
      std::unique_lock lock {mTasksAvailableMutex};
      mTasksAvailable.wait( lock,
        [this] ()
        {
          return
            mTasks.readableElementCount() > 0 ||
            mShutdownRequested.load(std::memory_order_relaxed) == true;
        } );

      if ( mShutdownRequested.load(std::memory_order_relaxed) == true )
        return;

      mThreads[threadId].isBusy.store(
        true, std::memory_order_release );

      auto& task = mTasks.pop();

      lock.unlock();

      task.task(threadId);

      mThreads[threadId].isBusy.store(
        false, std::memory_order_relaxed );
    }
  };

  for ( size_t threadId {}; threadId < threadCount; ++threadId )
    mThreads[threadId].thread = std::thread(
      workerTask, threadId, affinityOffset + threadId * 2 );
}

void
ThreadPool::deinit()
{
  {
    std::lock_guard lock {mTasksAvailableMutex};

    mShutdownRequested.store(
      true, std::memory_order_release );
  }

  mTasksAvailable.notify_all();

  waitForTasks();

  for ( size_t threadId {}; threadId < mThreads.length(); ++threadId )
    mThreads[threadId].thread.join();
}

void
ThreadPool::push(
  TaskPrototype&& task )
{
  mTasks.push({std::move(task)});

  std::lock_guard lock {mTasksAvailableMutex};
  mTasksAvailable.notify_one();
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
    threadCount = mThreads.length();


  size_t itersPerThread = iters / threadCount;

  if ( iters % threadCount != 0 )
    itersPerThread += 1;


  for ( std::size_t threadId {}, rangeEnd {}; threadId < threadCount; ++threadId )
  {
    const auto rangeStart =
      threadId * itersPerThread;

    rangeEnd = std::min(
      rangeStart + itersPerThread,
      iters );

    push(
    [task, rangeStart, rangeEnd] ( const std::size_t threadId )
    {
      task(rangeStart, rangeEnd);
    });
  }
}

bool
ThreadPool::doOneTask()
{
  if ( mTasks.readableElementCount() == 0 )
    return false;


  std::unique_lock lock {mTasksAvailableMutex};

  if ( mTasks.readableElementCount() == 0 )
  {
    lock.unlock();
    return false;
  }

  auto& task = mTasks.pop();

  lock.unlock();

  task.task(0);

  return true;
}

void
ThreadPool::waitForTasks()
{
  while ( doOneTask() == true )
    ;

  for ( size_t threadId {}; threadId < mThreads.length(); ++threadId )
  {
    auto& threadIsBusy = mThreads[threadId].isBusy;

    while ( threadIsBusy.load(std::memory_order_relaxed) == true )
      ;
  }
}
