// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include<cstdint>
#include<cstdio>
#include<cstring>
#include<ctime>

#ifndef DISKSPD_PERF_CLOCK_H
#define DISKSPD_PERF_CLOCK_H

namespace diskspd {

	/**
	 *	Static class for getting precise system timestamps.
	 *	Basically a wrapper around clock_gettime
	 *	Errors if the specified clock is not precise enough
	 */
	class PerfClock {

		public:

			/**
			 *	Set the system clock to be used.
			 *	Bails out if it's not precise enough
			 *	min_precision is in nanoseconds
			 */
			static void set_clock(clockid_t clk_id, long min_precision) {
				timespec res;
				memset(&res, 0, sizeof(res));
				int err = clock_getres(clk_id, &res);
				if (err) {
					perror("clock_getres failed");
					exit(1);
				}
				// a second is much too long. just check nanoseconds vs the min_precision
				if (res.tv_sec || res.tv_nsec > min_precision) {
					fprintf(stderr, "Clock not precise enough!\n");
					exit(1);
				}
				get_clock() = clk_id;
			}

			/**
			 *	Get abs time in nanoseconds
			 */
			static inline uint64_t get_time_ns() {
				timespec t;
				memset(&t, 0, sizeof(t));
				int err = clock_gettime(get_clock(), &t);
				if (err) {
					perror("clock_gettime failed");
					exit(1);
				}
				// convert timespec to nanoseconds
				return (uint64_t)((uint64_t)t.tv_sec*1000000000 + (uint64_t)t.tv_nsec);
			}

			/**
			 *	Get abs time in microseconds
			 */
			static inline uint64_t get_time_us() {
				return get_time_ns()/1000;
			}

			/**
			 *	Get abs time in milliseconds
			 */
			static inline uint64_t get_time_ms() {
				return get_time_ns()/1000000;
			}

		private:
			PerfClock(){}
			~PerfClock(){}

			static clockid_t& get_clock() {
				static clockid_t clock = CLOCK_MONOTONIC;
				return clock;
			}

	};

} //namespace diskspd

#endif // DISKSPD_PERF_CLOCK_H
