///
/// @file  pi_lehmer.cpp
///
/// Copyright (C) 2014 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include <primecount-internal.hpp>
#include <pmath.hpp>

#include <stdint.h>

namespace primecount {

/// Calculate the number of primes below x using Lehmer's formula.
/// Run time: O(x/(log x)^4) operations, O(x^(1/2)) space.
///
int64_t pi_lehmer(int64_t x, int threads)
{
  if (x < 2)
    return 0;

  int64_t y = iroot<4>(x);
  int64_t a = pi_meissel(y, /* threads = */ 1);
  int64_t phi_xa = phi(x, a, threads);
  int64_t p2 = P2_lehmer(x, a, threads);
  int64_t p3 = P3(x, a, threads);

  int64_t sum = phi_xa + a - 1 - p2 - p3;

  return sum;
}

/// Calculate the number of primes below x using Lehmer's formula.
/// This version uses a different P2(x, y) implementation,
/// it runs slower than pi_lehmer(x) on most systems.
/// Run time: O(x/(log x)^4) operations, O(x^(1/2) / log x) space.
///
int64_t pi_lehmer2(int64_t x, int threads)
{
  if (x < 2)
    return 0;

  int64_t y = iroot<4>(x);
  int64_t a = pi_meissel(y, /* threads = */ 1);
  int64_t phi_xa = phi(x, a, threads);
  int64_t p2 = P2(x, y, threads);
  int64_t p3 = P3(x, a, threads);

  int64_t sum = phi_xa + a - 1 - p2 - p3;

  return sum;
}

} // namespace primecount