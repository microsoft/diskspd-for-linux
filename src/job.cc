// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <vector>
#include <cstdio>
#include <chrono>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>

#include <sys/stat.h>
// TODO dynamic sector size discovery
//#include <sys/ioctl.h>
//#include <linux/fs.h>
//#include <sys/types.h>

#include "debug.h"
#include "profile.h"
#include "sys_info.h"
#include "job.h"
#include "target.h"
#include "thread.h"
#include "async_io.h"
#include "kernel_aio.h"

// benchmarking utils
#include "perf_clock.h"
#include "Histogram.h"
#include "IoBucketizer.h"

namespace diskspd {

	/**
	 *	Simply calls the appropriate ThreadParams thread_func
	 */
	static void * _thread_func(void * arg) {
		ThreadParams * params = static_cast<ThreadParams *>(arg);
		params->thread_func();
		return NULL;
	}

	/**
	 *	Run this job with the options supplied in the constructor
	 */
	bool Job::run_job() {

		/*******************
		 *	Set up Targets
		 *******************/

		// TODO move target setup to Profile or Target
		// TODO explore faster ways to set up files (async, copying files for multiple targets etc)

		// buffers to use when setting up targets
		const size_t fill_buf_size = 64*1024*1024;
		char * fill_buf = (char*)malloc(fill_buf_size*sizeof(char));
		// fill with ascending bytes
		for (unsigned i = 0; i < fill_buf_size; ++i) fill_buf[i] = (char)(i&0xFF);
		// and create a zero buffer
		char * zero_buf = (char*)calloc(fill_buf_size, sizeof(char));

		// set up target files
		v_printf("Setting up target files\n");
		for (auto& target : options->targets) {
			/*
			size_t block_sz;
			int rioctl = ioctl(fd, BLKSSZGET, &block_sz);
			if (rioctl == -1) {
				perror("ioctl error");
			}
			target->sector_size = block_sz;
			*/
			size_t sector_size = target->sector_size;

			// check alignment if target is going to be opened with O_DIRECT
			if (target->open_flags & O_DIRECT && (
							(target->block_size & (sector_size - 1)) ||		// -b
							(target->stride & (sector_size - 1)) ||			// -s and -r
							(target->thread_offset & (sector_size - 1))		// -T
							)) {
					fprintf(stderr, "O_DIRECT specified, but block size, stride or thread stride "
							"(-b, -s, -r, -T) argument isn't block aligned!\n");
					return false;
			}

			// how many threads will operate on this file?
			unsigned int no_threads =
				options->use_total_threads ? options->total_threads : target->threads_per_target;
			// what's the highest offset we can write to without going over the file size?
			off_t max_offset = target->max_size - target->base_offset - target->block_size;

			// check that none of our starting offsets for threads would be past the max size
			if (max_offset < target->thread_offset*(no_threads-1)) {
				fprintf(stderr,
						"File setup failed; I/O offset would overwrite end of file.\n"
						"Solution: reduce -T (more overlap between threads), or -t or -F (less "
						"threads per file), or increase file size (-c)\n");
				return false;
			}

			// now open the file and set it up
			int fd;

			if (target->create_file) {

				// remove the file first if it already exists
				fd = open(target->path.c_str(), O_WRONLY | O_SYNC, 0664);
				int rresult = remove(target->path.c_str());
				if (rresult && errno != ENOENT) {
					fprintf(stderr, "Failed to remove old file %s\n", target->path.c_str());
#ifdef ENABLE_DEBUG
					perror("remove old file failed");
#endif
					return false;
				}

				// create the file
				fd = open(target->path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_SYNC, 0664);
			} else {
				continue;
			}

			if (fd == -1) {
				fprintf(stderr, "Failed to open file %s\n", target->path.c_str());
#ifdef ENABLE_DEBUG
				perror("open file failed");
#endif
				return false;
			}

			// lseek to the base offset
			off_t lresult = lseek(fd, target->base_offset, SEEK_SET);
			if (lresult != target->base_offset) {
				fprintf(stderr, "Failed to setup file %s\n", target->path.c_str());
#ifdef ENABLE_DEBUG
				perror("lseek to base offset failed");
#endif
			}

			// fill with ascending bytes or zeros?
			char * buf_to_use = target->zero_buffers ? zero_buf : fill_buf;

			// fill the file up to its max size
			off_t remaining_bytes = target->max_size - target->base_offset;

			v_printf("	Laying out \"%s\"\n", target->path.c_str());
			while(remaining_bytes) {

				// how much to write on this loop
				size_t nbytes = fill_buf_size < remaining_bytes ? fill_buf_size : remaining_bytes;

				ssize_t wresult = write(fd, buf_to_use, nbytes);
				if (wresult != nbytes) {
					fprintf(stderr, "Failed to setup file %s\n", target->path.c_str());
#ifdef ENABLE_DEBUG
					perror("Write error");
					fprintf(stderr, "Write returned %ld\n", wresult);
#endif
					return false;
				}
				remaining_bytes -= nbytes;
			}
			close(fd);	
		}
		free(fill_buf);
		free(zero_buf);
	
		// get device name and scheduler for each target
		for (auto& target : options->targets) {
			struct stat buf = {0};
			int err = stat(target->path.c_str(), &buf);
			if (err) {
				perror("stat");
				exit(1);
			}
			printf("%s\n", target->path.c_str());
			printf("%u,%u\n", major(buf.st_rdev), minor(buf.st_rdev));
			printf("%u,%u\n", major(buf.st_dev), minor(buf.st_dev));

			// use appropriate device id (st_dev != st_rdev if target is a device)
			target->device = options->sys_info->device_from_id(buf.st_rdev ? buf.st_rdev : buf.st_dev);
			target->scheduler = options->sys_info->scheduler_from_device(target->device);
		}

		/***********************
		 *	Create ThreadParams
		 ***********************/

		// initialize results pointer
		results = std::make_shared<JobResults>();

		// prefix with a v to hopefully avoid confusion in this large function
		std::vector<std::shared_ptr<ThreadParams>> thread_params;

		// Generic initialization for each thread param
		for (unsigned int i = 0, id = 0; i < options->total_threads; ++i) {

			auto th = std::make_shared<ThreadParams>();

			// create results
			auto th_results = std::make_shared<ThreadResults>();

			// add to thread and job
			th->results = th_results;
			results->thread_results.push_back(th_results);

			// tell it what Job it's serving
			th->job = this;

			// set absolute id
			th->thread_id = th->results->thread_id = id++;

			th->job_options = options;
			th->io_manager = options->io_manager;

			// give it pointers to job synchronization structures
			th->run_threads = &run_threads;
			th->record_results = &record_results;
			th->thread_error = &thread_error;

			// push to the vector
			thread_params.push_back(th);
		}

		// count total overlap operations for io engine
		unsigned int total_overlap = 0;

		// index into thread_params for -t
		unsigned int index = 0;

		for (auto& target : options->targets) {

			// if -F, we want to go through all threads and add this target to each of them
			// if -t, we want to add this target to just some of them
			unsigned int loop_limit =
				options->use_total_threads ? options->total_threads : target->threads_per_target;

			for (
					unsigned int inner_index = 0;
					inner_index < loop_limit;
					++inner_index
				) {

				total_overlap += target->overlap;

				// get the thread param
				std::shared_ptr<ThreadParams> th;

				if (options->use_total_threads) {
					// -F, just use the inner index as it will go through all the threads
					th = thread_params[inner_index];
				} else {
					// -t, increment the outer index;
					// when the outer loop ends we will have assigned each thread one target
					th = thread_params[index++];
				}

				// Create a new target data for this target
				auto t_data = std::make_shared<TargetData>();
				t_data->target = target;

				// Create a new target results for this target
				auto t_results = std::make_shared<TargetResults>();
				t_results->target = target;

				// Tell the target data who its target results is
				t_data->results = t_results;
				// And who its thread is
				t_data->thread = th;

				// give the target data and results to the thread params and its results struct
				th->targets.push_back(t_data);
				th->results->target_results.push_back(t_results);

				// tell it who it is - this will match the absolute id if -F is specified
				// this operation is duplicated many times for -F but it's fine
				th->rel_thread_id = inner_index;

				// just a sanity check
				if (options->use_total_threads) {
					assert(th->rel_thread_id == th->thread_id);
				}

			}
		}

		// Maps of cpu ids to usage stats
		// each vector in these two contains absolute amount of time for user, nice, kernel, idle
		std::map<unsigned int, std::vector <double> > cpu_stats_init;
		std::map<unsigned int, std::vector <double> > cpu_stats_end;

		// start io_manager
		if (!options->io_manager->start(total_overlap)) {
			fprintf(stderr, "io engine failed to start\n");
			return false;
		}

		/*****************
		 *	Start threads
		 *****************/

		v_printf("Starting %u threads... ", options->total_threads);
		fflush(stdout);

		// we'll use this shared counter to keep track of which threads have completed initialization
		thread_counter = 0;

		// create a cpuset of the size needed for the number of cpus in the system
		cpu_set_t * cpu_set = CPU_ALLOC(options->sys_info->cpuhi + 1); // highest cpu id + 1
		size_t cpu_set_size = CPU_ALLOC_SIZE(options->sys_info->cpuhi + 1);

		auto cpuit = options->sys_info->affinity_cpus.begin();

		for (auto& t : thread_params) {
			int err = pthread_create(&t->thread_handle, NULL, _thread_func, static_cast<void *>(t.get()));
			if (err) {
				perror("Couldn't create pthread");
				return false;
			}
			// cpu affinity
			if (!options->disable_affinity) {

				// set the bit corresponding to the next cpu to affinitize to
				CPU_ZERO_S(cpu_set_size, cpu_set);
				CPU_SET_S(*cpuit, cpu_set_size, cpu_set);

				// actually set the affinity
				err = pthread_setaffinity_np(t->thread_handle, cpu_set_size, (const cpu_set_t*)cpu_set);

				if (err) {
					perror("Couldn't affinitize pthread");
					return false;
				}

				// affinitize round-robin by default; so loop back to the start
				++cpuit;
				if (cpuit == options->sys_info->affinity_cpus.end()) {
					cpuit = options->sys_info->affinity_cpus.begin();
				}
			}
		}
		CPU_FREE(cpu_set);

		// sleep on thread initialization, with a timeout in case of errors
		std::cv_status timeout_status;			// for getting result of wait_for
		std::chrono::milliseconds init_timeout(1);	 // the timeout for checking errors
		int timeout_counter = 10000;			   // actual timeout in seconds for initialization

		std::unique_lock<std::mutex> thread_lock(thread_mutex);

		// loop as long as all threads haven't been initialized
		while(thread_counter < options->total_threads) {

			timeout_status = thread_cv.wait_for(thread_lock, init_timeout);
			// if an error happened, get out
			if (thread_error) break;
			if (timeout_status == std::cv_status::timeout) {
				--timeout_counter;	// decrement the timeout counter - a second has passed
			}
			if (timeout_counter <= 0) {
				fprintf(stderr, "Thread initialization timed out!\n");
				return false;
			}
		}
		thread_lock.unlock();

		// check for error
		if (thread_error) {
			fprintf(stderr, "Error during thread initialization!\n");
			return false;
		}

		v_printf("All threads initialized\n");

		/*************
		 *	Warmup
		 *************/

		if(options->warmup_time) {
			v_printf("Warming up for %u second%s\n", options->warmup_time, options->warmup_time > 1 ? "s" : "");

			std::chrono::seconds warmupdur(options->warmup_time);

			// sleep until we're either woken up early (due to error) or the warmup is over
			std::unique_lock<std::mutex> thread_warmup_lock(thread_mutex);
			timeout_status = thread_error_cv.wait_for(thread_warmup_lock, warmupdur);
			thread_warmup_lock.unlock();

			if (timeout_status == std::cv_status::no_timeout || thread_error) {
				fprintf(stderr, "Error during warmup phase!\n");
				return false;
			}

			v_printf("Finished warming up; main test will run for %u second%s\n", options->duration, options->duration > 1 ? "s" : "");
		} else {
			v_printf("Performing main test for %u second%s\n", options->duration, options->duration > 1 ? "s" : "");
		}

		/*************
		 *	Duration
		 *************/

		// measure initial processor times
		cpu_stats_init = options->sys_info->get_cpu_stats();

		// get start time
		options->start_time_ns = PerfClock::get_time_ns();
		options->start_time_us = options->start_time_ns/1000;
		options->start_time_ms = options->start_time_us/1000;

		std::chrono::seconds maindur(options->duration);

		// sleep until we're either woken up early (due to error) or the duration is over
		std::unique_lock<std::mutex> thread_duration_lock(thread_mutex);

		// start recording data
		record_results = true;
		// sleep
		timeout_status = thread_error_cv.wait_for(thread_duration_lock, maindur);
		// stop recording data
		record_results = false;

		thread_duration_lock.unlock();

		if (timeout_status == std::cv_status::no_timeout || thread_error) {
			fprintf(stderr, "Error during main test!\n");
			return false;
		}

		// measure end processor times
		cpu_stats_end = options->sys_info->get_cpu_stats();

		// TODO cooldown (-C)

		/*
		 *************
		 *	Cleanup
		 *************
		 */

		// kill threads
		run_threads = false;

		// block on threads finishing
		for (auto& t : thread_params) {
			pthread_join(t->thread_handle, NULL);
		}

		// convert processor times to processor usage percentages
		results->cpu_usage_percentages.clear();

		for (auto& c: cpu_stats_init) {
			// resize the vector
			results->cpu_usage_percentages[c.first].resize(5);
			// copy the init and end vectors for this cpu to make this bit more readable
			std::vector<double> init, end;
			init = cpu_stats_init[c.first];
			end = cpu_stats_end[c.first];
			// differences between end and init will give the actual time in each state
			// remember 0 = user, 1 = nice, 2 = kernel, 3 = idle, 4 = iowait
			double total_time = (end[0]+end[1]+end[2]+end[3]+end[4]) - (init[0]+init[1]+init[2]+init[3]+init[4]);
            // total usage not including iowait or idle time
			double nonidle = (end[0]+end[1]+end[2]) - (init[0]+init[1]+init[2]);
			double user = (end[0]+end[1]) - (init[0]+init[1]);
			double kernel = (end[2]) - (init[2]);
			double iowait = (end[4]) - (init[4]);
			double idle = (end[3]) - (init[3]);

			results->cpu_usage_percentages[c.first][0] = nonidle/total_time;
			results->cpu_usage_percentages[c.first][1] = user/total_time;
			results->cpu_usage_percentages[c.first][2] = kernel/total_time;
			results->cpu_usage_percentages[c.first][3] = iowait/total_time;
			results->cpu_usage_percentages[c.first][4] = idle/total_time;
		}

		v_printf("Job done\n");
		return true;
	}
} // namespace diskspd
