// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>

#include <cstdint>

#include <pthread.h>
#include <fcntl.h>

#include "Histogram.h"
#include "IoBucketizer.h"
#include "target.h"
#include "async_io.h"
#include "sys_info.h"

#ifndef DISKSPD_JOB_H
#define DISKSPD_JOB_H

namespace diskspd {

	struct ThreadResults;
	class Profile;

	/**
	 *	The results of running a Job, updated at the end of a Job by the JobRunner
	 *	Used to produce final output by ResultParser
	 */
	struct JobResults {
		// each vector in this will contain total usage, userspace usage, kernel usage, idle usage
		std::map<unsigned int, std::vector<double> > cpu_usage_percentages;
		// total job time
		uint64_t total_time_ms; // ms or shorter? us?

		std::vector<std::shared_ptr<ThreadResults>> thread_results;
	};

	/**
	 *	User-provided options for running a job
	 */
	struct JobOptions {
		Profile* profile;

		std::shared_ptr<SysInfo> sys_info;

		std::shared_ptr<IAsyncIOManager> io_manager;

		unsigned int duration		= 10;
		unsigned int warmup_time	= 5;
		unsigned int cooldown_time	= 0;

		bool use_time_seed		= false;
		uint64_t rand_seed		= 0;

		// total threads in the job; used even if -F not specified
		unsigned int total_threads	  = 0;
		// -F - i.e. all threads operate on all targets
		bool use_total_threads			= false;

		bool disable_affinity			= false;
		bool measure_latency			= false;
		bool measure_iops_std_dev		= false;
		unsigned int io_bucket_duration_ms	= 1000;

		// abs starting time of the main test duration
		uint64_t start_time_ns			= 0;
		uint64_t start_time_us			= 0;
		uint64_t start_time_ms			= 0;

		std::vector<std::shared_ptr<Target>> targets;
	};

	/**
	 *	Represents a single 'Job' - a batch of tests to run and store results for
	 */
	class Job {
		public:
			Job(std::shared_ptr<JobOptions> options) :
				options(options),
				run_threads(true),
				record_results(false),
				thread_error(false){}

			/**
			 *	Run this job with the options supplied in the constructor
			 */
			bool run_job();

			/// getters used by ResultFormatter
			inline std::shared_ptr<JobOptions> get_options() const { return options; }
			inline std::shared_ptr<JobResults> get_results() const { return results; }

			/// sync primitives so we can wait for threads to finish initializing
			std::mutex thread_mutex;
			std::condition_variable thread_cv;
			volatile unsigned int thread_counter;

			// used by any thread to indicate a thread failure. Used with thread_mutex
			std::condition_variable thread_error_cv;

		private:
			// store the user-defined options for this Job
			std::shared_ptr<JobOptions> options;
			// store the results as the Job runs for the ResultFormatter to use later
			std::shared_ptr<JobResults> results;

			// used to stop I/O once duration+warmup+cooldown has been reached
			volatile bool run_threads;
			// used to denote whether to update thread results; for warming up/cooling down
			volatile bool record_results;
			// used to denote an error in a worker thread
			volatile bool thread_error;
	};

} // namespace diskspd

#endif // DISKSPD_JOB_H
