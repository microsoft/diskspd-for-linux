// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <vector>
#include <memory>
#include <cstdint>
#include <pthread.h>

#include "rng_engine.h"

#ifndef DISKSPD_THREAD_H
#define DISKSPD_THREAD_H

namespace diskspd {

	struct TargetResults;
	struct TargetData;
	class Job;
	struct JobOptions;
	class IAsyncIOManager;

	/**
	 *	Results collected by a single thread, updated as the Job runs
	 */
	struct ThreadResults {
		unsigned int thread_id;

		std::vector<std::shared_ptr<TargetResults>> target_results;
	};

	/**
	 *	Struct a thread uses to store its state, buffers etc
	 */
	struct ThreadParams {

		/// handle to the actual pthread that uses this ThreadParams
		pthread_t thread_handle;

		/// a thread is initialized when it has unblocked the main Job (so the warmup can start)
		bool initialized = false;

		/// absolute thread id
		unsigned int thread_id = 0;

		/// relative thread id for -t. If -F specified this is equal to thread_id
		unsigned int rel_thread_id = 0;

		// raw pointer to the current job; guaranteed to exist as long as this ThreadParams does
		// TODO get rid of this; currently used to get at cvs
		Job* job = nullptr;

		std::shared_ptr<IAsyncIOManager> io_manager;
		std::shared_ptr<JobOptions> job_options;

		// This is shared with a pointer stored in a vector in the Job
		std::shared_ptr<ThreadResults> results;

		// This is the only place the target data pointers are stored
		std::vector<std::shared_ptr<TargetData>> targets;

		// these don't need synchronization
		volatile bool * run_threads;
		volatile bool * record_results;
		volatile bool * thread_error;

		std::shared_ptr<RngEngine> rng_engine;		 // used for random offsets; default is seeded with 0
		std::shared_ptr<RngEngine> rw_rng_engine;	 // use for deciding read/write; seeded with random_device

		/**
		 *	Main thread function
		 */
		void thread_func();

		/**
		 *	Abort the Job and tell it that a thread failed
		 */
		void thread_abort();
	};
} // namespace diskspd

#endif // DISKSPD_THREAD_H
