///
/// @file  Sieve.cpp
/// @brief The Sieve class is a highly optimized sieve of
///        Eratosthenes implementation with 30 numbers per byte
///        i.e. the 8 bits of each byte correspond to the offsets
///        { 1, 7, 11, 13, 17, 19, 23, 29 }. This Sieve also
///        skips multiples of 2, 3, 5 using wheel factorization.
///
///        Unlike a traditional prime sieve this sieve is
///        designed for use in the combinatorial prime counting
///        algorithms: this sieve removes primes as well as
///        multiples of primes and it counts the number of
///        elements that have been crossed off for the first
///        time in the sieve array.
///
/// Copyright (C) 2018 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include <Sieve.hpp>
#include <SieveTables.hpp>
#include <popcnt.hpp>

#include <stdint.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

using namespace std;
using namespace primecount;

namespace {

struct WheelInit
{
  uint8_t factor;
  uint8_t index;
};

/// Categorize sieving primes according to their modulo 30
/// congruence class { 1, 7, 11, 13, 17, 19, 23, 29 }.
///
const array<int, 30> wheel_offsets =
{
  0, 8 * 0, 0, 0, 0, 0,
  0, 8 * 1, 0, 0, 0, 8 * 2,
  0, 8 * 3, 0, 0, 0, 8 * 4,
  0, 8 * 5, 0, 0, 0, 8 * 6,
  0, 0,     0, 0, 0, 8 * 7
};

/// Used to calculate the first multiple > start of a
/// sieving prime that is coprime to 2, 3, 5.
///
const WheelInit wheel_init[30] =
{
  {1,  0}, {0,  0}, {5,  1}, {4,  1}, {3,  1},
  {2,  1}, {1,  1}, {0,  1}, {3,  2}, {2,  2},
  {1,  2}, {0,  2}, {1,  3}, {0,  3}, {3,  4},
  {2,  4}, {1,  4}, {0,  4}, {1,  5}, {0,  5},
  {3,  6}, {2,  6}, {1,  6}, {0,  6}, {5,  7},
  {4,  7}, {3,  7}, {2,  7}, {1,  7}, {0,  7}
};

/// Small primes used for pre-sieving
const array<int, 10> primes = { 0, 2, 3, 5, 7, 11, 13, 17, 19, 23 };

/// Unset the n-th bit.
/// @return  1 if n-th bit was previously set, else 0
///
template <int n>
uint64_t unset_bit(byte_t* sieve)
{
  uint64_t is_bit = (*sieve >> n) & 1;
  *sieve &= ~(1 << n);
  return is_bit;
}

} // namespace

namespace primecount {

Sieve::Sieve(uint64_t start,
             uint64_t segment_size, 
             uint64_t wheel_size)
{
  assert(start % 30 == 0);
  assert(segment_size % 240 == 0);

  start_ = start;
  set_sieve_size(segment_size);
  wheel_.reserve(wheel_size);
  wheel_.resize(4);
}

/// The segment size (a.k.a. sieve distance) is sieve
/// size * 30 as each byte contains 30 numbers.
///
uint64_t Sieve::segment_size() const
{
  return sieve_.size() * 30;
}

/// segment_size must be a multiple of 240 as we
/// process 64-bit words (8 bytes) and each
/// byte contains 30 numbers.
///
uint64_t Sieve::get_segment_size(uint64_t size)
{
  size = max<uint64_t>(240, size);

  if (size % 240)
    size += 240 - size % 240;

  return size;
}

/// sieve size = segment_size / 30 as
/// each byte contains 30 numbers.
///
void Sieve::set_sieve_size(uint64_t segment_size)
{
  segment_size = get_segment_size(segment_size);
  uint64_t size = segment_size / 30;
  sieve_.resize(size);
}

/// Count 1 bits inside [start, stop]
uint64_t Sieve::count(uint64_t start, uint64_t stop) const
{
  if (start > stop)
    return 0;

  assert(stop - start < segment_size());

  uint64_t bit_count = 0;
  uint64_t start_idx = start / 240;
  uint64_t stop_idx = stop / 240;
  uint64_t m1 = unset_smaller[start % 240];
  uint64_t m2 = unset_larger[stop % 240];
  auto sieve = (uint64_t*) &sieve_[0];

  if (start_idx == stop_idx)
    bit_count = popcnt64(sieve[start_idx] & (m1 & m2));
  else
  {
    bit_count = popcnt64(sieve[start_idx] & m1);
    bit_count += popcnt(&sieve[start_idx + 1], stop_idx - (start_idx + 1));
    bit_count += popcnt64(sieve[stop_idx] & m2);
  }

  return bit_count;
}

/// Pre-sieve the multiples of the first
/// c primes inside [low, high[.
///
void Sieve::pre_sieve(uint64_t c, uint64_t low, uint64_t high)
{
  fill(sieve_.begin(), sieve_.end(), 0xff);

  uint64_t size = high - low;

  if (size < segment_size())
  {
    set_sieve_size(size);
    auto sieve = (uint64_t*) &sieve_[0];
    uint64_t back = size - 1;
    sieve[back / 240] &= unset_larger[back % 240];
  }

  assert(c < primes.size());

  for (uint64_t i = 4; i <= c; i++)
    cross_off(i, primes[i]);
}

/// Calculate the first multiple > start_ of prime
/// that is not divisible by 2, 3, 5 and
/// its wheel index.
///
void Sieve::add_wheel(uint64_t prime)
{
  assert(start_ % 30 == 0);

  // first multiple > start_
  uint64_t quotient = start_ / prime + 1;
  uint64_t multiple = prime * quotient;

  // find next multiple of prime that
  // is not divisible by 2, 3, 5
  uint64_t factor = wheel_init[quotient % 30].factor;
  multiple += prime * factor;
  multiple = (multiple - start_) / 30;

  // calculate wheel index of multiple
  uint32_t index = wheel_init[quotient % 30].index;
  index += wheel_offsets[prime % 30];

  wheel_.emplace_back((uint32_t) multiple, index);
}

/// Remove the i-th prime and the multiples of the i-th
/// prime from the sieve array. Returns the count of
/// elements removed for the first time i.e. the count of
/// sieved elements whose least prime factor is the
/// i-th prime. 
///
uint64_t Sieve::cross_off(uint64_t i, uint64_t prime)
{
  if (i >= wheel_.size())
    add_wheel(prime);

  uint64_t cnt = 0;
  uint32_t sieve_size = (uint32_t) sieve_.size();
  Wheel& wheel = wheel_[i];

  if (wheel.multiple >= sieve_size)
    wheel.multiple -= sieve_size;
  else
  {
    // pointer to the byte with the 1st multiple 
    byte_t* s = &sieve_[wheel.multiple];
    byte_t* sieve_end = &sieve_[sieve_size];
    prime /= 30;

    switch (wheel.index)
    {
      for (;;)
      {
        if (s >= sieve_end) { wheel.index = 0; break; }
        case 0: cnt += unset_bit<0>(s); s += prime * 6 + 0;
        if (s >= sieve_end) { wheel.index = 1; break; }
        case 1: cnt += unset_bit<1>(s); s += prime * 4 + 0;
        if (s >= sieve_end) { wheel.index = 2; break; }
        case 2: cnt += unset_bit<2>(s); s += prime * 2 + 0;
        if (s >= sieve_end) { wheel.index = 3; break; }
        case 3: cnt += unset_bit<3>(s); s += prime * 4 + 0;
        if (s >= sieve_end) { wheel.index = 4; break; }
        case 4: cnt += unset_bit<4>(s); s += prime * 2 + 0;
        if (s >= sieve_end) { wheel.index = 5; break; }
        case 5: cnt += unset_bit<5>(s); s += prime * 4 + 0;
        if (s >= sieve_end) { wheel.index = 6; break; }
        case 6: cnt += unset_bit<6>(s); s += prime * 6 + 0;
        if (s >= sieve_end) { wheel.index = 7; break; }
        case 7: cnt += unset_bit<7>(s); s += prime * 2 + 1;

        while (s + prime * 28 < sieve_end)
        {
          cnt += unset_bit<0>(s + prime *  0);
          cnt += unset_bit<1>(s + prime *  6);
          cnt += unset_bit<2>(s + prime * 10);
          cnt += unset_bit<3>(s + prime * 12);
          cnt += unset_bit<4>(s + prime * 16);
          cnt += unset_bit<5>(s + prime * 18);
          cnt += unset_bit<6>(s + prime * 22);
          cnt += unset_bit<7>(s + prime * 28);
          s += prime * 30 + 1;
        }
      }
      break;

      for (;;)
      {
        if (s >= sieve_end) { wheel.index =  8; break; }
        case  8: cnt += unset_bit<1>(s); s += prime * 6 + 1;
        if (s >= sieve_end) { wheel.index =  9; break; }
        case  9: cnt += unset_bit<5>(s); s += prime * 4 + 1;
        if (s >= sieve_end) { wheel.index = 10; break; }
        case 10: cnt += unset_bit<4>(s); s += prime * 2 + 1;
        if (s >= sieve_end) { wheel.index = 11; break; }
        case 11: cnt += unset_bit<0>(s); s += prime * 4 + 0;
        if (s >= sieve_end) { wheel.index = 12; break; }
        case 12: cnt += unset_bit<7>(s); s += prime * 2 + 1;
        if (s >= sieve_end) { wheel.index = 13; break; }
        case 13: cnt += unset_bit<3>(s); s += prime * 4 + 1;
        if (s >= sieve_end) { wheel.index = 14; break; }
        case 14: cnt += unset_bit<2>(s); s += prime * 6 + 1;
        if (s >= sieve_end) { wheel.index = 15; break; }
        case 15: cnt += unset_bit<6>(s); s += prime * 2 + 1;

        while (s + prime * 28 + 6 < sieve_end)
        {
          cnt += unset_bit<1>(s + prime *  0 + 0);
          cnt += unset_bit<5>(s + prime *  6 + 1);
          cnt += unset_bit<4>(s + prime * 10 + 2);
          cnt += unset_bit<0>(s + prime * 12 + 3);
          cnt += unset_bit<7>(s + prime * 16 + 3);
          cnt += unset_bit<3>(s + prime * 18 + 4);
          cnt += unset_bit<2>(s + prime * 22 + 5);
          cnt += unset_bit<6>(s + prime * 28 + 6);
          s += prime * 30 + 7;
        }
      }
      break;

      for (;;)
      {
        if (s >= sieve_end) { wheel.index = 16; break; }
        case 16: cnt += unset_bit<2>(s); s += prime * 6 + 2;
        if (s >= sieve_end) { wheel.index = 17; break; }
        case 17: cnt += unset_bit<4>(s); s += prime * 4 + 2;
        if (s >= sieve_end) { wheel.index = 18; break; }
        case 18: cnt += unset_bit<0>(s); s += prime * 2 + 0;
        if (s >= sieve_end) { wheel.index = 19; break; }
        case 19: cnt += unset_bit<6>(s); s += prime * 4 + 2;
        if (s >= sieve_end) { wheel.index = 20; break; }
        case 20: cnt += unset_bit<1>(s); s += prime * 2 + 0;
        if (s >= sieve_end) { wheel.index = 21; break; }
        case 21: cnt += unset_bit<7>(s); s += prime * 4 + 2;
        if (s >= sieve_end) { wheel.index = 22; break; }
        case 22: cnt += unset_bit<3>(s); s += prime * 6 + 2;
        if (s >= sieve_end) { wheel.index = 23; break; }
        case 23: cnt += unset_bit<5>(s); s += prime * 2 + 1;

        while (s + prime * 28 + 10 < sieve_end)
        {
          cnt += unset_bit<2>(s + prime *  0 +  0);
          cnt += unset_bit<4>(s + prime *  6 +  2);
          cnt += unset_bit<0>(s + prime * 10 +  4);
          cnt += unset_bit<6>(s + prime * 12 +  4);
          cnt += unset_bit<1>(s + prime * 16 +  6);
          cnt += unset_bit<7>(s + prime * 18 +  6);
          cnt += unset_bit<3>(s + prime * 22 +  8);
          cnt += unset_bit<5>(s + prime * 28 + 10);
          s += prime * 30 + 11;
        }     
      }
      break;

      for (;;)
      {
        if (s >= sieve_end) { wheel.index = 24; break; }
        case 24: cnt += unset_bit<3>(s); s += prime * 6 + 3;
        if (s >= sieve_end) { wheel.index = 25; break; }
        case 25: cnt += unset_bit<0>(s); s += prime * 4 + 1;
        if (s >= sieve_end) { wheel.index = 26; break; }
        case 26: cnt += unset_bit<6>(s); s += prime * 2 + 1;
        if (s >= sieve_end) { wheel.index = 27; break; }
        case 27: cnt += unset_bit<5>(s); s += prime * 4 + 2;
        if (s >= sieve_end) { wheel.index = 28; break; }
        case 28: cnt += unset_bit<2>(s); s += prime * 2 + 1;
        if (s >= sieve_end) { wheel.index = 29; break; }
        case 29: cnt += unset_bit<1>(s); s += prime * 4 + 1;
        if (s >= sieve_end) { wheel.index = 30; break; }
        case 30: cnt += unset_bit<7>(s); s += prime * 6 + 3;
        if (s >= sieve_end) { wheel.index = 31; break; }
        case 31: cnt += unset_bit<4>(s); s += prime * 2 + 1;

        while (s + prime * 28 + 12 < sieve_end)
        {
          cnt += unset_bit<3>(s + prime *  0 +  0);
          cnt += unset_bit<0>(s + prime *  6 +  3);
          cnt += unset_bit<6>(s + prime * 10 +  4);
          cnt += unset_bit<5>(s + prime * 12 +  5);
          cnt += unset_bit<2>(s + prime * 16 +  7);
          cnt += unset_bit<1>(s + prime * 18 +  8);
          cnt += unset_bit<7>(s + prime * 22 +  9);
          cnt += unset_bit<4>(s + prime * 28 + 12);
          s += prime * 30 + 13;
        }
      }
      break;

      for (;;)
      {
        if (s >= sieve_end) { wheel.index = 32; break; }
        case 32: cnt += unset_bit<4>(s); s += prime * 6 + 3;
        if (s >= sieve_end) { wheel.index = 33; break; }
        case 33: cnt += unset_bit<7>(s); s += prime * 4 + 3;
        if (s >= sieve_end) { wheel.index = 34; break; }
        case 34: cnt += unset_bit<1>(s); s += prime * 2 + 1;
        if (s >= sieve_end) { wheel.index = 35; break; }
        case 35: cnt += unset_bit<2>(s); s += prime * 4 + 2;
        if (s >= sieve_end) { wheel.index = 36; break; }
        case 36: cnt += unset_bit<5>(s); s += prime * 2 + 1;
        if (s >= sieve_end) { wheel.index = 37; break; }
        case 37: cnt += unset_bit<6>(s); s += prime * 4 + 3;
        if (s >= sieve_end) { wheel.index = 38; break; }
        case 38: cnt += unset_bit<0>(s); s += prime * 6 + 3;
        if (s >= sieve_end) { wheel.index = 39; break; }
        case 39: cnt += unset_bit<3>(s); s += prime * 2 + 1;

        while (s + prime * 28 + 16 < sieve_end)
        {
          cnt += unset_bit<4>(s + prime *  0 +  0);
          cnt += unset_bit<7>(s + prime *  6 +  3);
          cnt += unset_bit<1>(s + prime * 10 +  6);
          cnt += unset_bit<2>(s + prime * 12 +  7);
          cnt += unset_bit<5>(s + prime * 16 +  9);
          cnt += unset_bit<6>(s + prime * 18 + 10);
          cnt += unset_bit<0>(s + prime * 22 + 13);
          cnt += unset_bit<3>(s + prime * 28 + 16);
          s += prime * 30 + 17;
        }
      }
      break;

      for (;;)
      {
        if (s >= sieve_end) { wheel.index = 40; break; }
        case 40: cnt += unset_bit<5>(s); s += prime * 6 + 4;
        if (s >= sieve_end) { wheel.index = 41; break; }
        case 41: cnt += unset_bit<3>(s); s += prime * 4 + 2;
        if (s >= sieve_end) { wheel.index = 42; break; }
        case 42: cnt += unset_bit<7>(s); s += prime * 2 + 2;
        if (s >= sieve_end) { wheel.index = 43; break; }
        case 43: cnt += unset_bit<1>(s); s += prime * 4 + 2;
        if (s >= sieve_end) { wheel.index = 44; break; }
        case 44: cnt += unset_bit<6>(s); s += prime * 2 + 2;
        if (s >= sieve_end) { wheel.index = 45; break; }
        case 45: cnt += unset_bit<0>(s); s += prime * 4 + 2;
        if (s >= sieve_end) { wheel.index = 46; break; }
        case 46: cnt += unset_bit<4>(s); s += prime * 6 + 4;
        if (s >= sieve_end) { wheel.index = 47; break; }
        case 47: cnt += unset_bit<2>(s); s += prime * 2 + 1;

        while (s + prime * 28 + 18 < sieve_end)
        {
          cnt += unset_bit<5>(s + prime *  0 +  0);
          cnt += unset_bit<3>(s + prime *  6 +  4);
          cnt += unset_bit<7>(s + prime * 10 +  6);
          cnt += unset_bit<1>(s + prime * 12 +  8);
          cnt += unset_bit<6>(s + prime * 16 + 10);
          cnt += unset_bit<0>(s + prime * 18 + 12);
          cnt += unset_bit<4>(s + prime * 22 + 14);
          cnt += unset_bit<2>(s + prime * 28 + 18);
          s += prime * 30 + 19;
        }
      }
      break;

      for (;;)
      {
        if (s >= sieve_end) { wheel.index = 48; break; }
        case 48: cnt += unset_bit<6>(s); s += prime * 6 + 5;
        if (s >= sieve_end) { wheel.index = 49; break; }
        case 49: cnt += unset_bit<2>(s); s += prime * 4 + 3;
        if (s >= sieve_end) { wheel.index = 50; break; }
        case 50: cnt += unset_bit<3>(s); s += prime * 2 + 1;
        if (s >= sieve_end) { wheel.index = 51; break; }
        case 51: cnt += unset_bit<7>(s); s += prime * 4 + 4;
        if (s >= sieve_end) { wheel.index = 52; break; }
        case 52: cnt += unset_bit<0>(s); s += prime * 2 + 1;
        if (s >= sieve_end) { wheel.index = 53; break; }
        case 53: cnt += unset_bit<4>(s); s += prime * 4 + 3;
        if (s >= sieve_end) { wheel.index = 54; break; }
        case 54: cnt += unset_bit<5>(s); s += prime * 6 + 5;
        if (s >= sieve_end) { wheel.index = 55; break; }
        case 55: cnt += unset_bit<1>(s); s += prime * 2 + 1;

        while (s + prime * 28 + 22 < sieve_end)
        {
          cnt += unset_bit<6>(s + prime *  0 +  0);
          cnt += unset_bit<2>(s + prime *  6 +  5);
          cnt += unset_bit<3>(s + prime * 10 +  8);
          cnt += unset_bit<7>(s + prime * 12 +  9);
          cnt += unset_bit<0>(s + prime * 16 + 13);
          cnt += unset_bit<4>(s + prime * 18 + 14);
          cnt += unset_bit<5>(s + prime * 22 + 17);
          cnt += unset_bit<1>(s + prime * 28 + 22);
          s += prime * 30 + 23;
        }
      }
      break;

      for (;;)
      {
        if (s >= sieve_end) { wheel.index = 56; break; }
        case 56: cnt += unset_bit<7>(s); s += prime * 6 + 6;
        if (s >= sieve_end) { wheel.index = 57; break; }
        case 57: cnt += unset_bit<6>(s); s += prime * 4 + 4;
        if (s >= sieve_end) { wheel.index = 58; break; }
        case 58: cnt += unset_bit<5>(s); s += prime * 2 + 2;
        if (s >= sieve_end) { wheel.index = 59; break; }
        case 59: cnt += unset_bit<4>(s); s += prime * 4 + 4;
        if (s >= sieve_end) { wheel.index = 60; break; }
        case 60: cnt += unset_bit<3>(s); s += prime * 2 + 2;
        if (s >= sieve_end) { wheel.index = 61; break; }
        case 61: cnt += unset_bit<2>(s); s += prime * 4 + 4;
        if (s >= sieve_end) { wheel.index = 62; break; }
        case 62: cnt += unset_bit<1>(s); s += prime * 6 + 6;
        if (s >= sieve_end) { wheel.index = 63; break; }
        case 63: cnt += unset_bit<0>(s); s += prime * 2 + 1;

        while (s + prime * 28 + 28 < sieve_end)
        {
          cnt += unset_bit<7>(s + prime *  0 +  0);
          cnt += unset_bit<6>(s + prime *  6 +  6);
          cnt += unset_bit<5>(s + prime * 10 + 10);
          cnt += unset_bit<4>(s + prime * 12 + 12);
          cnt += unset_bit<3>(s + prime * 16 + 16);
          cnt += unset_bit<2>(s + prime * 18 + 18);
          cnt += unset_bit<1>(s + prime * 22 + 22);
          cnt += unset_bit<0>(s + prime * 28 + 28);
          s += prime * 30 + 29;
        }
      }
      break;
    }

    // update for the next segment
    wheel.multiple = (uint32_t) (s - sieve_end);
  }

  return cnt;
}

} // namespace
