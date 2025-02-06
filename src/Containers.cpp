#include "Containers.hpp"


void
Swapchain::swap(
  std::size_t& bufferIndex )
{
  auto oldMiddle = mMiddle.load(std::memory_order_acquire);

  while ( mMiddle.compare_exchange_strong(oldMiddle, bufferIndex) == false )
    ;

  bufferIndex = oldMiddle;
}

void
Swapchain::swap()
{
  swap(mBack);
}

void
Swapchain::retire()
{
  swap(mFront);
}

std::size_t
Swapchain::back() const
{
  return mBack;
}

std::size_t
Swapchain::front() const
{
  return mFront;
}
