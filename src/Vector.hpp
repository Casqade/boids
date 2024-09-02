#pragma once


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


Vector3 operator + ( const Vector3& lhs, const Vector3& rhs );
Vector3 operator - ( const Vector3& lhs, const Vector3& rhs );
Vector3 operator * ( const Vector3& lhs, const Vector3::value_type& rhs );
Vector3 operator / ( const Vector3& lhs, const Vector3::value_type& rhs );


Vector3 operator * ( const Vector3::value_type& lhs, const Vector3& rhs );

Vector3& operator += ( Vector3& lhs, const Vector3& rhs );
Vector3& operator -= ( Vector3& lhs, const Vector3& rhs );
Vector3& operator *= ( Vector3& lhs, const Vector3::value_type& rhs );
Vector3& operator /= ( Vector3& lhs, const Vector3::value_type& rhs );
