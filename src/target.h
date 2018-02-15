// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdlib>	// calloc
#include <memory>
#include <mutex>
#include <pthread.h>
#include <assert.h>
#include <fcntl.h>

#include "thread.h"
#include "rng_engine.h"
#include "Histogram.h"
#include "IoBucketizer.h"

#ifndef DISKSPD_TARGET_H
#define DISKSPD_TARGET_H

namespace diskspd {

	/**
	 *	Represents a file or device to read/write from
	 */
	struct Target {
		enum TargetType {
			REGULAR_FILE,
			BLOCK_DEVICE,
			UNKNOWN
		};

		Target(std::string p) : path(p) {}
		std::string path;
		off_t size					= 0;			// in bytes
		TargetType type;	// TODO may not be needed beyond initialization;
							// Linux doesn't care too much about whether it's a device

		// TODO get this dynamically somehow (stat/statfs, then ioctl on the mounted device)
		size_t sector_size			= 512;			// physical block size of storage (to support O_DIRECT)

		// these options need to be per-target because in future we may want the user to be able to
		// specify them per-target with XML or JSON
		bool create_file			= false;		// create this file or use existing..?
		size_t block_size			= 64 * 1024;	// -b
		off_t base_offset			= 0;			// -B
		off_t max_size				= 0;			// -f

		unsigned int overlap		= 2;			// -o

		off_t thread_offset			= 0;			// -T
		off_t stride				= 64 * 1024;	// -s, also the random alignment

		int open_flags				= O_RDWR;

		// use_rand > interlocked_seq > default (use stride)
		bool use_random_alignment	= false;		// -r
		bool use_interlocked		= false;		// -si

		unsigned int write_percentage	= 0;		// -w

		unsigned int threads_per_target = 1;		// -t

		bool zero_buffers			= false;		// -Zz
		bool rand_buffers			= false;		// -Zr
		bool separate_buffers		= false;		// -Zs

		off_t max_throughput		= 0;			// -g, 0 denotes no throttling

		// interlocked offset shared by all threads working on this file
		off_t interlocked_offset	= 0;			// si
		std::mutex interlocked_mutex;				// si
	};

	/*
	 *	Per-target-per-thread results
	 */
	struct TargetResults {

		// which target these results pertain to
		std::shared_ptr<Target> target;

		// how many bytes were transferred in all I/O
		uint64_t bytes_count = 0;
		uint64_t read_bytes_count = 0;
		uint64_t write_bytes_count = 0;

		// # iops
		uint64_t iops_count = 0;
		uint64_t read_iops_count = 0;
		uint64_t write_iops_count = 0;

		// microsecond resolution (us)
		Histogram<uint64_t> read_latency_histogram;
		Histogram<uint64_t> write_latency_histogram;

		// microsecond resolution (us)
		IoBucketizer read_bucketizer;
		IoBucketizer write_bucketizer;

	};

	/**
	 *	A class to safely encapsulate and align target buffers
	 */
	class TargetBuffer {
		public:
			/**
			 *	Align the buffer pointer to align bytes
			 */
			TargetBuffer(size_t size, size_t align) : _sz(size), _align(align) {
				new_buffer();
			}
			/**
			 *	No alignment
			 */
			TargetBuffer(size_t size) : TargetBuffer (size, 1) {}
			/**
			 *	Default
			 */
			TargetBuffer(){}
			/**
			 *	Copy constructor
			 */
			TargetBuffer(const TargetBuffer& t) : _sz(t._sz), _align(t._align) {
				new_buffer();
				// copy the buffer data
				std::memcpy(_ptr, t._ptr, t._sz);
			}
			/**
			 *	Copy constructor =
			 */
			TargetBuffer& operator=(const TargetBuffer& t) {
				_sz = t._sz;
				_align = t._align;
				new_buffer();
				// copy the buffer data
				std::memcpy(_ptr, t._ptr, t._sz);
				return *this;
			}
			/**
			 *	Destructor
			 */
			~TargetBuffer() {
				if (_sz) {
					free(_unaligned);
				}
			}
			/**
			 *	Re-mallocer to avoid using the copy constructor
			 */
			void calloc(size_t size, size_t align) {
				if (_sz) {
					free(_unaligned);
				}
				_sz = size;
				_align = align;
				_unaligned = nullptr;
				_ptr = nullptr;
				new_buffer();
			}
			/**
			 *	Fill the buffer with random bytes
			 */
			void fill_rand(std::shared_ptr<RngEngine> rng_engine) {
				for (size_t i = 0; i < _sz; ++i) {
					(static_cast<unsigned char *>(_ptr))[i] =
						(unsigned char)rng_engine->get_rand_offset(256);
				}
			}
			/**
			 *	Fill the buffer with ascending bytes
			 */
			void fill_default() {
				for (size_t i = 0; i < _sz; ++i) {
					(static_cast<unsigned char *>(_ptr))[i] = (unsigned char)(i % 256);
				}
			}

			inline void * ptr() const { return _ptr; }	// get the pointer to the buffer
			inline size_t size() const { return _sz; }

		private:
			void new_buffer() {
				// don't allocate a buffer if no size
				if(!_sz) return;

				// check align is a power of 2 or == 1
				assert(_align && (_align == 1 || !(_align & (_align - 1))));
				// allocate enough space to guarantee we can produce an aligned buffer
				_unaligned = std::calloc(_sz+_align-1, 1);
				assert(_unaligned);

				_ptr = reinterpret_cast<void *>(((size_t)_unaligned + _align - 1) & ~(_align - 1));
			}
			void * _unaligned = nullptr;	// pointer to real base of buffer
			void * _ptr = nullptr;			// pointer to aligned base of buffer
			size_t _sz = 0;					// size of aligned buffer
			size_t _align;
	};

	/**
	 *	per-thread target-related data
	 */
	struct TargetData {

		// the thread we belong to
		std::shared_ptr<ThreadParams> thread;

		// shared with pointers in the Job/JobResults
		std::shared_ptr<Target> target;
		std::shared_ptr<TargetResults> results;

		// file descriptor for an open target (we only open a target once per thread)
		int fd = -1;

		// used for reading and writing, unless -Z<size> is specified
		TargetBuffer buffer;
		// for -Z<size>
		TargetBuffer write_buffer;

		// pointer to the thread's rng engine
		std::shared_ptr<RngEngine> rng_engine;

		/**
		 *	Get the offset at which a thread should start doing I/O on this target. No bounds
		 *	checking - that is done when the job sets up targets
		 *	Note that if -si is specified, thread_offset will always be 0
		 */
		inline off_t get_thread_base_offset() {
			return target->base_offset + thread->rel_thread_id*target->thread_offset;
		}

		/**
		 *	If an iop would overflow the max_size, correct it to the base offset for this thread
		 */
		inline off_t correct_overflow(off_t curr_offset) {

			if ((curr_offset + target->block_size) > target->max_size) {
				return get_thread_base_offset();
			}
			return curr_offset;
		}


		/**
		 *	Get the initial offset for an iop on a file
		 */
		inline off_t get_start_offset() {

			if (target->use_random_alignment) {
				return random_offset();
			} else if (target->use_interlocked) {
				// if -si is specified, thread_offset is 0, and the interlocked offset is initially
				// set to the base_offset. So this is the same as get_next_offset
				return get_next_offset(0);
			}
			return get_thread_base_offset();
		}

		/**
		 *	Get the next offset for an iop, based on the old offset
		 */
		inline off_t get_next_offset(off_t curr_offset) {

			// -r
			if (target->use_random_alignment) {
				return random_offset();

			// -si
			} else if (target->use_interlocked) {
				// ignore curr_offset!
				target->interlocked_mutex.lock();
				// increment and correct the interlocked offset
				target->interlocked_offset =
					correct_overflow(target->interlocked_offset + target->stride);
				// copy the value so we don't get race conditions after unlocking
				off_t offset = target->interlocked_offset;
				target->interlocked_mutex.unlock();
				return offset;
			}

			// otherwise, just do the regular stride increment
			return correct_overflow(curr_offset + target->stride);
		}

		/**
		 *	Get a random offset aligned at the stride
		 *	Doesn't produce offsets that would overwrite the end of the file (checks block size)
		 */
		inline off_t random_offset() {
			off_t alignment = target->stride;
			// generate a random offset aligned to random_alignment in the [base_offset,max_size) interval
			off_t interval = target->max_size - target->base_offset - target->block_size;
			interval -= (interval % alignment);
			off_t range = interval/alignment + 1;

			off_t rnd =rng_engine->get_rand_offset(range);

			return target->base_offset + rnd*alignment;
		}
	};
} // namespace diskspd
#endif // DISKSPD_TARGET_H
