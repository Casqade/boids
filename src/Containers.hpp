#pragma once

#include "Allocators.hpp"

#include <atomic>
#include <memory>
#include <cassert>
#include <cstddef>
#include <utility>


template <typename T, std::size_t Alignment = sizeof(std::size_t)>
class Array
{
  T* mData {};
  std::size_t mLength {};
  AllocatorArena* mAllocator {};


public:

  Array() = default;
  Array( Array&& ) noexcept;
  Array( const Array& ) = delete;

  Array( AllocatorArena&,
    const std::size_t length,
    const T& defaultValue = {} ) noexcept;

  Array( AllocatorArena&,
    const std::initializer_list <T>& ) noexcept;

  ~Array() noexcept;


  Array& operator = ( Array&& ) noexcept;

  T& operator [] ( const std::size_t index ) noexcept;
  const T& operator [] ( const std::size_t index ) const noexcept;

  T* data() const noexcept;
  std::size_t length() const noexcept;
  std::size_t alignment() const noexcept;
};

template <typename T, std::size_t Alignment>
Array <T, Alignment>::Array(
  Array&& other ) noexcept
{
  *this = std::move(other);
}

template <typename T, std::size_t Alignment>
Array <T, Alignment>::Array(
  AllocatorArena& allocator,
  const std::size_t length,
  const T& defaultValue ) noexcept
  : mLength{length}
  , mAllocator{&allocator}
{
  if ( mLength == 0 )
    return;

  mData = allocator.allocate <T> (mLength, Alignment);

  assert(mData != nullptr);
  new (mData) T[length]{};
}

template <typename T, std::size_t Alignment>
Array <T, Alignment>::Array(
  AllocatorArena& allocator,
  const std::initializer_list <T>& list ) noexcept
  : mLength{list.size()}
  , mAllocator{&allocator}
{
  if ( mLength == 0 )
    return;

  mData = allocator.allocate <T> (mLength, Alignment);

  assert(mData != nullptr);
  std::copy(list.begin(), list.end(), mData);
}

template <typename T, std::size_t Alignment>
Array <T, Alignment>::~Array() noexcept
{
  std::destroy_n(mData, mLength);

  if ( mAllocator != nullptr )
    mAllocator->deallocate(mData, mLength);
}

template <typename T, std::size_t Alignment>
Array <T, Alignment>& Array <T, Alignment>::operator = (
  Array&& other ) noexcept
{
  std::swap(mData, other.mData);
  std::swap(mLength, other.mLength);
  std::swap(mAllocator, other.mAllocator);

  return *this;
}

template <typename T, std::size_t Alignment>
T& Array <T, Alignment>::operator [] (
  const std::size_t index ) noexcept
{
  assert(index < mLength);

  return mData[index];
}

template <typename T, std::size_t Alignment>
const T& Array <T, Alignment>::operator [] (
  const std::size_t index ) const noexcept
{
  assert(index < mLength);

  return mData[index];
}

template <typename T, std::size_t Alignment>
T* Array <T, Alignment>::data() const noexcept
{
  return mData;
}

template <typename T, std::size_t Alignment>
std::size_t Array <T, Alignment>::length() const noexcept
{
  return mLength;
}

template <typename T, std::size_t Alignment>
std::size_t Array <T, Alignment>::alignment() const noexcept
{
  return Alignment;
}


constexpr std::size_t CacheLineSize = 64;

// Single Producer, Multiple consumers
template <typename Data>
class RingBuffer
{
  Array <Data, CacheLineSize> mData {};

  std::atomic_size_t mIndexWriter {};
  alignas(CacheLineSize) std::atomic_size_t mReadableElements {};
  alignas(CacheLineSize) std::atomic_size_t mIndexReader {};


public:
  RingBuffer() = default;
  RingBuffer( RingBuffer&& ) = delete;
  RingBuffer( const RingBuffer& ) = delete;


  void init( AllocatorArena&, size_t bufferSize );

  void push( Data&& );
  Data& pop();

  size_t readableElementCount() const;
};

template <typename Data>
void
RingBuffer <Data>::init(
  AllocatorArena& allocator,
  size_t bufferSize )
{
  assert(mData.length() == 0);

  mData = {allocator, bufferSize};
}

template <typename Data>
void
RingBuffer <Data>::push(
  Data&& data )
{
  const auto index = mIndexWriter.fetch_add(
    1, std::memory_order_relaxed ) % mData.length();

  mData[index] = data;

  mReadableElements.fetch_add(
    1, std::memory_order_release );
}

template <typename Data>
Data&
RingBuffer <Data>::pop()
{
  const bool canRead =
    mReadableElements.fetch_sub(
      1, std::memory_order_acquire );

  assert(canRead == true);

  if ( canRead == false )
  {
    static Data nullValue {};
    return nullValue;
  }

  const auto indexReader = mIndexReader.fetch_add(
    1, std::memory_order_relaxed );

  return mData[indexReader % mData.length()];
}

template <typename Data>
size_t
RingBuffer <Data>::readableElementCount() const
{
  return mReadableElements.load(
    std::memory_order_relaxed );
}

