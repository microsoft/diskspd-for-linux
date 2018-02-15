// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <vector>
#include <memory>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <unistd.h>

#include "debug.h"
#include "async_io.h"
#include "profile.h"
#include "sys_info.h"
#include "job.h"
#include "target.h"
#include "thread.h"

#include "perf_clock.h"
#include "Histogram.h"
#include "IoBucketizer.h"

namespace diskspd {

	void ThreadParams::thread_abort() {
		*run_threads = false;						// stop all the other threads
		*thread_error = true;						// signal to Job that a thread failed
		if (initialized) {
			job->thread_error_cv.notify_one();		// wake the Job thread
		}
	}

	void ThreadParams::thread_func() {

		/***********
		 *	Setup
		 ***********/

		// Initialize rng engine
		if (job_options->use_time_seed) {
			rng_engine = std::make_shared<RngEngine>();
		} else {
			rng_engine = std::make_shared<RngEngine>(job_options->rand_seed);
		}
		rw_rng_engine = std::make_shared<RngEngine>();

		// count total overlap for io_engine initialization
		size_t total_overlap = 0;

		// figure out how many buckets to put in the read/write bucketizers
		uint64_t bucket_duration = 0;
		size_t valid_buckets = 0;

		if (job_options->measure_iops_std_dev) {
			bucket_duration = (uint64_t)job_options->io_bucket_duration_ms;
			// represents the number of valid buckets in the bucketizer
			valid_buckets = (size_t)std::ceil((double)(job_options->duration * 1000) / (double)bucket_duration);
		}

		// open all the files etc
		for (auto& t_data : targets) {

			// give rng engine pointer to the target data for calculating random offsets
			t_data->rng_engine = rng_engine;

			total_overlap += t_data->target->overlap;

			// initialize bucketizers for iops stddev
			if (job_options->measure_iops_std_dev) {
				t_data->results->read_bucketizer.Initialize(bucket_duration, valid_buckets);
				t_data->results->write_bucketizer.Initialize(bucket_duration, valid_buckets);
			}

			// open an instance of this target and put it in the TargetData
			t_data->fd = open(t_data->target->path.c_str(), t_data->target->open_flags);
			if (t_data->fd == -1) {
				perror("Failed to open target");
				thread_abort();
				return;
			}

			// create and initialize I/O buffers
			// we need the buffer to be large enough for all overlapped requests
			// we align it to the device block size if necessary
			t_data->buffer.calloc(
					t_data->target->overlap*t_data->target->block_size,
					t_data->target->open_flags & O_DIRECT ? t_data->target->sector_size : 1
					);

			// fill buffers with appropriate data
			if (t_data->target->rand_buffers) {
				t_data->buffer.fill_rand(rng_engine);

			} else if (!t_data->target->zero_buffers) {
				t_data->buffer.fill_default();
			}

			// -Zs - create a separate write buffer
			if (t_data->target->separate_buffers) {

				// initialize a separate write buffer for this target
				t_data->write_buffer.calloc(
						t_data->target->block_size,
						t_data->target->open_flags & O_DIRECT ? t_data->target->sector_size : 1
						);

				// fill write buffer with appropriate data
				if (t_data->target->rand_buffers) {
					t_data->write_buffer.fill_rand(rng_engine);

				} else if (!t_data->target->zero_buffers) {
					t_data->write_buffer.fill_default();
				}
			}
		}

		/*****************
		 *	Initialize IO
		 *****************/

		// create the group for the io manager
		if (!io_manager->create_group(thread_id, total_overlap)) {
			thread_abort();
			return;
		}

		// generate I/O request details
		int aio_result = 0;

		// how much this thread throttles its throughput
		// NOTE: this is clearly incorrect, but it matches the windows version of diskspd
		off_t thread_throughput = targets[0]->target->max_throughput;

		for (auto& t_data : targets) {

			off_t curr_offset = t_data->get_start_offset();
			//thread_throughput += t_data->target->max_throughput; // see above

			// for i in range(-o)
			for (unsigned int i = 0; i < t_data->target->overlap; ++i) {

				// get the index into the buffer corresponding to this overlap
				void * read_buf = static_cast<void *>(
							&(
								static_cast<char *>(
									t_data->buffer.ptr())[i*t_data->target->block_size]
							)
						);

				IAsyncIop::Type aio_type;

				// by default, write buffer is the same as read
				void * write_buf = read_buf;
				// use a different buffer for writes if -Zs specified
				if(t_data->target->separate_buffers) {
					write_buf = t_data->write_buffer.ptr();
				}

				// decide read or write
				if (rw_rng_engine->get_percentage() <= t_data->target->write_percentage) {
					aio_type = IAsyncIop::Type::WRITE;

				} else {
					aio_type = IAsyncIop::Type::READ;
				}

				// create an object to represent this op
				std::shared_ptr<IAsyncIop> op = io_manager->construct(
						aio_type,
						t_data->fd,
						curr_offset,
						read_buf,
						write_buf,
						t_data->target->block_size,
						thread_id,		// group id should be thread-unique, so just use thread_id
						t_data,
						PerfClock::get_time_us()
						);

				// enqueue it with the io manager
				aio_result = io_manager->enqueue(op);

				if (aio_result) {
					perror("aio enqueue failed");
					thread_abort();
					return;
				}

				curr_offset = t_data->get_next_offset(curr_offset);
			}
		}

		// submit the enqueued ops
		aio_result = io_manager->submit(thread_id);
		if (aio_result) {
			perror("aio submit failed");
			thread_abort();
			return;
		}

		// Unblock main thread (so the job can start the warmup/duration)
		std::unique_lock<std::mutex> thread_lock(job->thread_mutex);
		job->thread_counter++;
		thread_lock.unlock();
		job->thread_cv.notify_one();

		initialized = true;

		/************
		 *	Do Work
		 ************/

		off_t thread_bytes_count = 0;

		// wait on ops finishing, restarting them at a new offset
		while(*run_threads) {

			// Throughput throttling (-g)
			// We check the total throughput on this thread
			// If it's too high, we sleep for a millisecond
			// This matches the behavior of the Windows version
			if (*record_results && thread_throughput) {
				// get time since start of duration
				uint64_t since_start_ms = PerfClock::get_time_ms() - job_options->start_time_ms;

				if (since_start_ms != 0) {
					// get an estimate of throughput by counting the bytes
					off_t curr_bytes_per_ms = thread_bytes_count/since_start_ms;
					// throttle if we're over the max
					if (curr_bytes_per_ms > thread_throughput) {
						usleep(1000);
						continue;
					}
				}
			}

			// block until an operation completes
			std::shared_ptr<IAsyncIop> op = io_manager->wait(thread_id);

			// potentially exit right after waiting for io - improves accuracy of duration
			if (!*run_threads) break;

			// which target was this op for?
			std::shared_ptr<TargetData> t_data = op->get_target_data();

			// check for errors in the result
			int err = op->get_errno();
			int ret = op->get_ret();
			if (err != 0) {
				fprintf(stderr, "aio error: %s\n", strerror(err));
				thread_abort();
				return;
			}
			if (ret != t_data->target->block_size) {
				fprintf(stderr, "ret from aio not equal to block size, it's %d\n", ret);
				thread_abort();
				return;
			}

			uint64_t abs_time_us = PerfClock::get_time_us();

			// record results if we're in the main duration
			if (*record_results) {

				// throughput monitoring for the whole thread
				thread_bytes_count += t_data->target->block_size;

				t_data->results->bytes_count += ret;
				++t_data->results->iops_count;

				uint64_t since_start_us = 0;	// time since start of duration
				uint64_t op_time_us = 0;		// time this op took to complete

				if (job_options->measure_iops_std_dev || job_options->measure_latency) {
					since_start_us = abs_time_us - job_options->start_time_us;
					op_time_us = abs_time_us - op->get_time();
				}

				if (op->get_type() == IAsyncIop::Type::READ) {

					++t_data->results->read_iops_count;
					t_data->results->read_bytes_count += ret;

					if (job_options->measure_iops_std_dev) {
						t_data->results->read_bucketizer.Add(since_start_us/1000);
					}

					if (job_options->measure_latency) {
						t_data->results->read_latency_histogram.Add(op_time_us);
					}

				} else {

					++t_data->results->write_iops_count;
					t_data->results->write_bytes_count += ret;

					if (job_options->measure_iops_std_dev) {
						t_data->results->write_bucketizer.Add(since_start_us/1000);
					}

					if (job_options->measure_latency) {
						t_data->results->write_latency_histogram.Add(op_time_us);
					}

				}
			}

			// update op time
			op->set_time(abs_time_us);

			// update op offset
			op->set_offset(t_data->get_next_offset(op->get_offset()));

			//v_printf("Starting op at %lu\n", op->get_offset());

			// change op type. this will switch from read to write buffer if necessary
			if (rw_rng_engine->get_percentage() <= t_data->target->write_percentage) {
				op->set_type(IAsyncIop::Type::WRITE);

			} else {
				op->set_type(IAsyncIop::Type::READ);

			}

			// re-queue and submit it
			aio_result = io_manager->enqueue(op);
			if (aio_result) {
				perror("aio enqueue failed");
				thread_abort();
				return;
			}
			aio_result = io_manager->submit(thread_id);
			if (aio_result) {
				perror("aio submit failed");
				thread_abort();
				return;
			}
		}

		// release resources
		for (auto& t_data : targets) {
			close(t_data->fd);
		}

		v_printf("Ending thread %d\n", thread_id);
	}
} // namespace diskspd
