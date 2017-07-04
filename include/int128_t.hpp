///
/// @file  int128_t.hpp
/// @brief Additional integer types used in primecount:
///        int128_t, uint128_t, intfast64_t, intfast128_t,
///        maxint_t, maxuint_t.
///
/// Copyright (C) 2017 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#ifndef INT128_T_HPP
#define INT128_T_HPP

#include <stdint.h>
#include <limits>
#include <type_traits>

/// The __int128_t type (GCC/Clang) is not well supported by
/// the C++ standard library (in 2016) so we have to define
/// some functions ourselves. We also define typedefs so we
/// can use int128_t instead of __int128_t. Once this is done
/// int128_t can be used like a regular integer type.
///
#if defined(HAVE___INT128_T)
#define HAVE_INT128_T

#include <ostream>
#include <string>
#include <sstream>

namespace primecount {

using int128_t = __int128_t;
using uint128_t = __uint128_t;
using maxint_t = __int128_t;
using maxuint_t = __uint128_t;

inline std::ostream& operator<<(std::ostream& stream, uint128_t n)
{
  std::string str;

  while (n > 0)
  {
    str += '0' + n % 10;
    n /= 10;
  }
  if (str.empty())
    str = "0";

  stream << std::string(str.rbegin(), str.rend());
  return stream;
}

inline std::ostream& operator<<(std::ostream& stream, int128_t n)
{
  if (n < 0)
  {
    stream << "-";
    n = -n;
  }
  stream << (uint128_t) n;
  return stream;
}

template <typename T>
inline std::string to_string(T x)
{
  std::ostringstream ss;
  ss << x;
  return ss.str();
}

} // namespace

#elif defined(HAVE_INT128_T)

namespace primecount {

using maxint_t = int128_t;
using maxuint_t = uint128_t;

}

#else /* int128_t not supported */

namespace primecount {

using maxint_t = int64_t;
using maxuint_t = uint64_t;

}

#endif

namespace primecount {

/// Fastest 64-bit integer type for division.
/// On most Intel CPUs before 2015 unsigned 64-bit division
/// is about 10 percent faster than signed division. It is
/// likely that in a few years signed and unsigned division
/// will run equally fast.
///
using intfast64_t = uint64_t;

#if defined(HAVE_INT128_T)

/// Fastest 128-bit integer type for division.
/// On the author's Intel Core-i7 4770 CPU from 2013 using
/// uint128_t instead of int128_t gives 10 percent better
/// performance.
///
using intfast128_t = uint128_t;

#endif

/// Portable namespace, includes functions which (unlike the
/// versions form the C++ standard library) work with the
/// int128_t and uint128_t types (2014).
///
namespace prt {

template <typename T>
struct numeric_limits
{
  static constexpr T max()
  {
    return std::numeric_limits<T>::max();
  }
};

#if defined(HAVE_INT128_T)

template <>
struct numeric_limits<int128_t>
{
  static constexpr int128_t max()
  {
    return ~(((int128_t) 1) << 127);
  }
};

template <>
struct numeric_limits<uint128_t>
{
  static constexpr uint128_t max()
  {
    return ~((uint128_t) 0);
  }
};

#endif

template <typename T>
struct make_signed
{
#if !defined(HAVE_INT128_T)
  typedef typename std::make_signed<T>::type type;
#else
  typedef typename std::conditional<std::is_same<T, uint8_t>::value, int8_t,
          typename std::conditional<std::is_same<T, uint16_t>::value, int16_t,
          typename std::conditional<std::is_same<T, uint32_t>::value, int32_t,
          typename std::conditional<std::is_same<T, uint64_t>::value, int64_t,
          typename std::conditional<std::is_same<T, uint128_t>::value, int128_t,
          T>::type>::type>::type>::type>::type type;
#endif
};

template <typename T>
struct is_integral
{
  enum
  {
#if !defined(HAVE_INT128_T)
    value = std::is_integral<T>::value
#else
    value = std::is_integral<T>::value ||
            std::is_same<T, int128_t>::value ||
            std::is_same<T, uint128_t>::value
#endif
  };
};

template <typename T>
struct is_signed
{
  enum
  {
#if !defined(HAVE_INT128_T)
    value = std::is_signed<T>::value
#else
    value = std::is_signed<T>::value ||
            std::is_same<T, int128_t>::value
#endif
  };
};

template <typename T>
struct is_unsigned
{
  enum
  {
#if !defined(HAVE_INT128_T)
    value = std::is_unsigned<T>::value
#else
    value = std::is_unsigned<T>::value ||
            std::is_same<T, uint128_t>::value
#endif
  };
};

} // namespace prt
} // namespace primecount

#endif
