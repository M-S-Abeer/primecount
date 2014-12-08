///
/// @file  S2_sieve.cpp
/// @brief Calculate the contribution of the special leaves which
///        require a sieve in the Deleglise-Rivat algorithm.
///        This is a parallel implementation which uses compression
///        (PiTable & FactorTable) to reduce the memory usage by
///        about 10x.
///
/// Copyright (C) 2014 Kim Walisch, <kim.walisch@gmail.com>
///
/// This file is distributed under the BSD License. See the COPYING
/// file in the top level directory.
///

#include <PiTable.hpp>
#include <FactorTable.hpp>
#include <primecount-internal.hpp>
#include <int128.hpp>
#include <pmath.hpp>
#include <BitSieve.hpp>
#include <tos_counters.hpp>
#include <S2LoadBalancer.hpp>
#include <S2Status.hpp>

#include <stdint.h>
#include <algorithm>
#include <iostream>
#include <vector>

#ifdef _OPENMP
  #include <omp.h>
#endif

using namespace std;
using namespace primecount;

namespace S2_sieve {

/// For each prime calculate its first multiple >= low
template <typename T>
vector<int64_t> generate_next_multiples(int64_t low, int64_t size, vector<T>& primes)
{
  vector<int64_t> next;

  next.reserve(size);
  next.push_back(0);

  for (int64_t b = 1; b < size; b++)
  {
    int64_t prime = primes[b];
    int64_t next_multiple = ceil_div(low, prime) * prime;
    next_multiple += prime * (~next_multiple & 1);
    next.push_back(next_multiple);
  }

  return next;
}

/// Cross-off the multiples of prime in the sieve array.
/// For each element that is unmarked the first time update
/// the special counters tree data structure.
///
template <typename T>
void cross_off(int64_t prime,
               int64_t low,
               int64_t high,
               int64_t& next_multiple,
               BitSieve& sieve,
               T& counters)
{
  int64_t segment_size = sieve.size();
  int64_t k = next_multiple;

  for (; k < high; k += prime * 2)
  {
    if (sieve[k - low])
    {
      sieve.unset(k - low);
      cnt_update(counters, k - low, segment_size);
    }
  }
  next_multiple = k;
}

/// Compute the S2 contribution of the special leaves that require
/// a sieve. Each thread processes the interval
/// [low_thread, low_thread + segments * segment_size[
/// and the missing special leaf contributions for the interval
/// [1, low_process[ are later reconstructed and added in
/// the parent S2_sieve() function.
///
template <typename T, typename P, typename F>
T S2_sieve_thread(T x,
                  int64_t y,
                  int64_t z,
                  int64_t c,
                  int64_t segment_size,
                  int64_t segments_per_thread,
                  int64_t thread_num,
                  int64_t low,
                  int64_t limit,
                  FactorTable<F>& factors,
                  PiTable& pi,
                  vector<P>& primes,
                  vector<int64_t>& mu_sum,
                  vector<int64_t>& phi)
{
  low += segment_size * segments_per_thread * thread_num;
  limit = min(low + segment_size * segments_per_thread, limit);
  int64_t pi_sqrty = pi[isqrt(y)];
  int64_t max_prime = min(min(isqrt(x / low), y), isqrt(z));
  int64_t pi_max = pi[max_prime];
  T S2_thread = 0;

  BitSieve sieve(segment_size);
  vector<int32_t> counters(segment_size);
  vector<int64_t> next = generate_next_multiples(low, pi_max + 1, primes);
  phi.resize(pi_max + 1, 0);
  mu_sum.resize(pi_max + 1, 0);

  // Segmeted sieve of Eratosthenes
  for (; low < limit; low += segment_size)
  {
    // Current segment = interval [low, high[
    int64_t high = min(low + segment_size, limit);
    int64_t b = c + 1;

    // check if we need the sieve
    if (c <= pi_max)
    {
      sieve.fill(low, high);

      // phi(y, i) nodes with i <= c do not contribute to S2, so we
      // simply sieve out the multiples of the first c primes
      for (int64_t i = 2; i <= c; i++)
      {
        int64_t k = next[i];
        for (int64_t prime = primes[i]; k < high; k += prime * 2)
          sieve.unset(k - low);
        next[i] = k;
      }

      // Initialize special tree data structure from sieve
      cnt_finit(sieve, counters, segment_size);
    }

    // For c + 1 <= b <= pi_sqrty
    // Find all special leaves: n = primes[b] * m, with mu[m] != 0 and primes[b] < lpf[m]
    // which satisfy: low <= (x / n) < high
    for (int64_t end = min(pi_sqrty, pi_max); b <= end; b++)
    {
      int64_t prime = primes[b];
      int64_t min_m = max(min(x / ((T) prime * (T) high), y), y / prime);
      int64_t max_m = min(x / ((T) prime * (T) low), y);

      if (prime >= max_m)
        goto next_segment;

      factors.to_index(&min_m);
      factors.to_index(&max_m);

      for (int64_t m = max_m; m > min_m; m--)
      {
        if (prime < factors.lpf(m))
        {
          int64_t n = prime * factors.get_number(m);
          int64_t xn = (int64_t) (x / n);
          int64_t count = cnt_query(counters, xn - low);
          int64_t phi_xn = phi[b] + count;
          int64_t mu_m = factors.mu(m);
          S2_thread -= mu_m * phi_xn;
          mu_sum[b] -= mu_m;
        }
      }

      phi[b] += cnt_query(counters, (high - 1) - low);
      cross_off(prime, low, high, next[b], sieve, counters);
    }

    // For pi_sqrty <= b <= pi_sqrtz
    // Find all hard special leaves: n = primes[b] * primes[l]
    // which satisfy: low <= (x / n) < high
    for (; b <= pi_max; b++)
    {
      int64_t prime = primes[b];
      int64_t l = pi[min(min(x / ((T) prime * (T) low), y), z / prime)];
      int64_t min_hard_leaf = max3(min(x / ((T) prime * (T) high), y), y / prime, prime);

      if (prime >= primes[l])
        goto next_segment;

      for (; primes[l] > min_hard_leaf; l--)
      {
        int64_t n = prime * primes[l];
        int64_t xn = x / n;
        int64_t count = cnt_query(counters, xn - low);
        int64_t phi_xn = phi[b] + count;
        S2_thread += phi_xn;
        mu_sum[b]++;
      }

      phi[b] += cnt_query(counters, (high - 1) - low);
      cross_off(prime, low, high, next[b], sieve, counters);
    }

    next_segment:;
  }

  return S2_thread;
}

/// Calculate the contribution of the special leaves which require
/// a sieve (in order to reduce the memory usage).
/// This is a parallel implementation with advanced load balancing.
/// As most special leaves tend to be in the first segments we
/// start off with a small segment size and few segments
/// per thread, after each iteration we dynamically increase
/// the segment size and the segments per thread.
///
template <typename T, typename P, typename F>
T S2_sieve(T x,
           int64_t y,
           int64_t z,
           int64_t c,
           T s2_sieve_approx,
           PiTable& pi,
           vector<P>& primes,
           FactorTable<F>& factors,
           int threads)
{
  if (print_status())
  {
    cout << endl;
    cout << "=== S2_sieve(x, y) ===" << endl;
    cout << "Computation of the special leaves requiring a sieve" << endl;
  }

  double time = get_wtime();
  T s2_sieve = 0;
  int64_t low = 1;
  int64_t limit = z + 1;

  S2Status status;
  S2LoadBalancer loadBalancer(x, limit, threads);
  int64_t segment_size = loadBalancer.get_min_segment_size();
  int64_t segments_per_thread = 1;
  vector<int64_t> phi_total(pi[min(isqrt(z), y)] + 1, 0);

  while (low < limit)
  {
    int64_t segments = ceil_div(limit - low, segment_size);
    threads = in_between(1, threads, segments);
    segments_per_thread = in_between(1, segments_per_thread, ceil_div(segments, threads));

    aligned_vector<vector<int64_t> > phi(threads);
    aligned_vector<vector<int64_t> > mu_sum(threads);
    aligned_vector<double> timings(threads);

    #pragma omp parallel for num_threads(threads) reduction(+: s2_sieve)
    for (int i = 0; i < threads; i++)
    {
      timings[i] = get_wtime();
      s2_sieve += S2_sieve_thread(x, y, z, c, segment_size, segments_per_thread,
          i, low, limit, factors, pi, primes, mu_sum[i], phi[i]);
      timings[i] = get_wtime() - timings[i];
    }

    // Once all threads have finished reconstruct and add the 
    // missing contribution of all special leaves. This must
    // be done in order as each thread (i) requires the sum of
    // the phi values from the previous threads.
    //
    for (int i = 0; i < threads; i++)
    {
      for (size_t j = 1; j < phi[i].size(); j++)
      {
        s2_sieve += phi_total[j] * (T) mu_sum[i][j];
        phi_total[j] += phi[i][j];
      }
    }

    low += segments_per_thread * threads * segment_size;
    loadBalancer.update(low, threads, &segment_size, &segments_per_thread, timings);

    if (print_status())
      status.print(s2_sieve, s2_sieve_approx, loadBalancer.get_rsd());
  }

  if (print_status())
    print_result("S2_sieve", s2_sieve, time);

  return s2_sieve;
}

} // namespace S2_sieve

namespace primecount {

int64_t S2_sieve(int64_t x,
                 int64_t y,
                 int64_t z,
                 int64_t c,
                 int64_t s2_sieve_approx,
                 PiTable& pi,
                 vector<int32_t>& primes,
                 FactorTable<uint16_t>& factors,
                 int threads)
{
  return S2_sieve::S2_sieve(x, y, z, c, s2_sieve_approx, pi, primes, factors, threads);
}

#ifdef HAVE_INT128_T

int128_t S2_sieve(uint128_t x,
                  int64_t y,
                  int64_t z,
                  int64_t c,
                  uint128_t s2_sieve_approx,
                  PiTable& pi,
                  vector<uint32_t>& primes,
                  FactorTable<uint16_t>& factors,
                  int threads)
{
  return S2_sieve::S2_sieve(x, y, z, c, s2_sieve_approx, pi, primes, factors, threads);
}

int128_t S2_sieve(uint128_t x,
                  int64_t y,
                  int64_t z,
                  int64_t c,
                  uint128_t s2_sieve_approx,
                  PiTable& pi,
                  vector<int64_t>& primes,
                  FactorTable<uint32_t>& factors,
                  int threads)
{
  return S2_sieve::S2_sieve(x, y, z, c, s2_sieve_approx, pi, primes, factors, threads);
}

#endif

} // namespace primecount
