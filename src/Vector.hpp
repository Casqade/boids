#pragma once

#include <cstring>
#include <cmath>
#include <x86intrin.h>

struct Vector3
{
  using value_type = float;

  value_type x {};
  value_type y {};
  value_type z {};


  value_type length() const;
  value_type length_squared() const;

  Vector3 normalized() const;
  Vector3 normalized_fast() const;
  value_type distance( const Vector3& ) const;

  value_type dot( const Vector3& ) const;
  Vector3 cross( const Vector3& ) const;

};

Vector3
operator + (
  const Vector3& lhs,
  const Vector3& rhs )
{
  return
  {
    lhs.x + rhs.x,
    lhs.y + rhs.y,
    lhs.z + rhs.z,
  };
}

Vector3
operator - (
  const Vector3& lhs,
  const Vector3& rhs )
{
  return
  {
    lhs.x - rhs.x,
    lhs.y - rhs.y,
    lhs.z - rhs.z,
  };
}

Vector3
operator * (
  const Vector3& lhs,
  const Vector3::value_type& rhs )
{
  return
  {
    lhs.x * rhs,
    lhs.y * rhs,
    lhs.z * rhs,
  };
}

Vector3
operator / (
  const Vector3& lhs,
  const Vector3::value_type& rhs )
{
  return
  {
    lhs.x / rhs,
    lhs.y / rhs,
    lhs.z / rhs,
  };
}


Vector3
operator * (
  const Vector3::value_type& lhs,
  const Vector3& rhs )
{
  return
  {
    lhs * rhs.x,
    lhs * rhs.y,
    lhs * rhs.z,
  };
}

Vector3&
operator += (
  Vector3& lhs,
  const Vector3& rhs )
{
  lhs = lhs + rhs;
  return lhs;
}

Vector3&
operator -= (
  Vector3& lhs,
  const Vector3& rhs )
{
  lhs = lhs - rhs;
  return lhs;
}

Vector3&
operator *= (
  Vector3& lhs,
  const Vector3::value_type& rhs )
{
  lhs = lhs * rhs;
  return lhs;
}

Vector3&
operator /= (
  Vector3& lhs,
  const Vector3::value_type& rhs )
{
  lhs = lhs / rhs;
  return lhs;
}

Vector3::value_type
Vector3::length() const
{
  return std::sqrt( length_squared() );
};

Vector3::value_type
Vector3::length_squared() const
{
  return x * x + y * y + z * z;
  return dot(*this);
}

Vector3
Vector3::normalized() const
{
  const auto len = length();

  if ( len != 0.f )
    return *this / len;

  return {};
}

Vector3::value_type
Vector3::distance(
  const Vector3& other ) const
{
  return (other - *this).length();
}

Vector3::value_type
Vector3::dot(
  const Vector3& other ) const
{
  return x * other.x + y * other.y + z * other.z;
}
