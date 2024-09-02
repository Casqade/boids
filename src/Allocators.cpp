#include "Allocators.hpp"

#include <cassert>
#include <cstdlib>


#define IsPowerOfTwo(integer) \
  !( integer != 1 && integer & (integer - 1) )


AllocatorArena::~AllocatorArena()
{
  assert(mStart == nullptr);
}

bool
AllocatorArena::reserve(
  const std::size_t bytes,
  AllocatorArena* parent ) noexcept
{
  assert(mStart == nullptr);

  if ( parent == nullptr )
    mStart = std::malloc(bytes);
  else
    mStart = parent->allocate <std::byte> (bytes);

  if ( mStart == nullptr )
    return false;

  mCurrent = mStart;
  mEnd = static_cast <std::byte*> (mStart) + bytes;

  return true;
}

void
AllocatorArena::free(
  AllocatorArena* parent ) noexcept
{
  assert(mStart != nullptr);

  if ( parent == nullptr )
    std::free(mStart);
  else
    parent->deallocate(
      static_cast <std::byte*> (mStart),
      bytesReserved() );

  mStart = {};
  mEnd = {};
  mCurrent = {};
}

std::size_t
AllocatorArena::bytesLeft() const noexcept
{
  return
    static_cast <std::byte*> (mEnd) -
    static_cast <std::byte*> (mCurrent);
}

std::size_t
AllocatorArena::bytesReserved() const noexcept
{
  return
    static_cast <std::byte*> (mEnd) -
    static_cast <std::byte*> (mStart);
}
