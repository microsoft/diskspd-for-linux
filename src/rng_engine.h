/*
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License.
 */

#include <cstdint>
#include <random>

#ifndef DISKSPD_RNG_ENGINE_H
#define DISKSPD_RNG_ENGINE_H

namespace diskspd {

	/**
	 *	Class for seeding and generating random numbers for use in diskspd
	 *	NOT threadsafe; we instantiate one of these for each thread
	 */
	class RngEngine {

		public:
			/**
			 *	Initialize with default seed (random - better than time(NULL))
			 */
			RngEngine(){
				static std::random_device rd;
				engine = std::mt19937(rd());
				engine64 = std::mt19937_64(rd());
			}
			/**
			 *	Initialize with user-specified seed
			 */
			RngEngine(uint64_t seed){
				engine = std::mt19937(seed);
				engine64 = std::mt19937_64(seed);
			}

			/**
			 *	Get a random number for use as a random file offset in the range [0,size)
			 */
			inline off_t get_rand_offset(off_t size) {
				std::uniform_int_distribution<> dist(0, size-1);
				return dist(engine64);
			}

			/**
			 *	Get a random number from 1-100 for determining write percentage
			 */
			inline unsigned int get_percentage() {
				static std::uniform_int_distribution<> dist(1, 100);
				unsigned int ret = dist(engine);
				return ret;
			}

		private:
			std::mt19937_64 engine64;
			std::mt19937 engine;

	};

} //namespace diskspd

#endif // DISKSPD_RNG_ENGINE_H
