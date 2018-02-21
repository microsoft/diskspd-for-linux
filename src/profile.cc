// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>

#include "debug.h"
#include "profile.h"
#include "sys_info.h"
#include "job.h"
#include "target.h"
#include "thread.h"
#include "options.h"
#include "posix_aio.h"
#include "kernel_aio.h"

namespace diskspd
{

	// these have to be defined somewhere! here is good
	bool verbose(false);
	bool debug(false);

	bool Profile::parse_options(int argc, char ** argv) {

		assert(argc >= 1);
		assert(argv != nullptr);
		assert(argv[0] != nullptr);

		// construct command line string to be used in results
		for (int i = 0; i < argc-1; ++i) { // omit last element
			cmd_line += argv[i];
			cmd_line += ' ';
		}
		cmd_line += argv[argc-1]; // add last element on the end, without a trailing space

		// create an Options object, passing it our command line args for parsing
		Options options;
		bool result = options.parse_args(argc, argv);
		if (!result) {
#ifdef ENABLE_DEBUG
			fprintf(stderr, "Couldn't parse arguments\n");
#endif
			return false;
		}

		// TODO JSON/XML input options would be checked here, possibly producing a number of Jobs

		// Otherwise, there is a single Job, which will be populated by the rest of this function
		std::shared_ptr<JobOptions> job_options = std::make_shared<JobOptions>();

		// Get targets
		std::vector<std::string> non_opts = options.get_non_opts();
		if (non_opts.size() == 0) {
			fprintf(stderr, "No targets specified!\n");
			return false;
		}
		for (auto& s : non_opts) {
			job_options->targets.push_back(std::make_shared<Target>(s));
		}

		const char * curr_arg = nullptr;

		// since we can't specify options per-target in the command line, we'll store everything in
		// this dummy and then apply it to all the targets at the end
		Target dummy("");

		// -a

		// initialize SysInfo struct
		sys_info = std::make_shared<SysInfo>();

		// use the argument to create an affinity set if there is one
		if (curr_arg = options.get_arg(CPU_AFFINITY)) {
			sys_info->init_sys_info(curr_arg);
		} else {
			sys_info->init_sys_info(nullptr);
		}

		// -b
		// The options object knows that this is a byte size, so it'll apply the correct multiplier
		options.arg_to_number<size_t>(BLOCK_SIZE, 1, &dummy.block_size);

		// -B
		options.arg_to_number<off_t>(BASE_OFFSET, dummy.block_size, &dummy.base_offset);

		// -c
		if (options.arg_to_number<off_t>(CREATE_FILES, dummy.block_size, &dummy.size)) {
			dummy.create_file = true;
		}

		/*
		CDTIME
		*/

		// -d
		options.arg_to_number<unsigned int>(DURATION, 0, &job_options->duration);

		// -D
		if (options.arg_to_number<unsigned int>(DIOPS, 0, &job_options->io_bucket_duration_ms)) {
			job_options->measure_iops_std_dev = true;
		}

		// -f
		options.arg_to_number<off_t>(MAX_SIZE, dummy.block_size, &dummy.max_size);

		// -F
		if (options.arg_to_number<unsigned int>(TOTAL_THREADS, 0, &job_options->total_threads)) {
			if (options.get_arg(THREADS_PER_TARGET)) {
				fprintf(stderr, "Can't use -t and -F at the same time!\n");
				return false;
			}
			job_options->use_total_threads = true;
			// option isn't relevant if this is set
			dummy.threads_per_target = 0;
		}

		// -g
		options.arg_to_number<off_t>(MAX_THROUGHPUT, dummy.block_size, &dummy.max_throughput);

		// -L
		if (options.get_arg(LATENCY)) {
			job_options->measure_latency = true;
		}

		// -n
		if (options.get_arg(NO_AFFINITY)) {
			job_options->disable_affinity = true;
		}

		// -o
		options.arg_to_number<unsigned int>(OVERLAP, 0, &dummy.overlap);

		// -r and -s
		if (options.arg_to_number<off_t>(RANDOM_ALIGN, dummy.block_size, &dummy.stride)) {
			// empty argument will be converted to 0, just change to block size
			if (dummy.stride == 0) {
				dummy.stride = dummy.block_size;
			}

			dummy.use_random_alignment = true;

		} else if (curr_arg = options.get_arg(SEQUENTIAL_STRIDE)) {

			// interlocked flag means we do checks manually for this one
			if (curr_arg[0] == 'i') {
				dummy.use_interlocked = true;
				// go to next char
				curr_arg = &curr_arg[1];
			}
			if (*curr_arg) {
				if (!Options::valid_byte_size(curr_arg)) {
					fprintf(stderr, "Error in stride argument\n");
					return false;
				}
				dummy.stride = static_cast<off_t>(Options::byte_size_from_arg(curr_arg, dummy.block_size));
			} else {
				// default; stride = block size
				dummy.stride = dummy.block_size;
			}

		} else {
			// default; stride = block size
			dummy.stride = dummy.block_size;
		}

		// -S
		if (curr_arg = options.get_arg(CACHING_OPTIONS)) {

			// look at each character in the arg string
			for (;curr_arg[0] != '\0'; curr_arg = &curr_arg[1]) {

				// set both and break
				if (curr_arg[0] == 'h') {
					dummy.open_flags |= O_DIRECT | O_SYNC;
					break;
				} else if (curr_arg[0] == 's') {
					dummy.open_flags |= O_SYNC;
				} else if (curr_arg[0] == 'd') {
					dummy.open_flags |= O_DIRECT;
				} else {
					fprintf(stderr, "Invalid or unimplemented caching option -S%c\n", curr_arg[0]);
					return false;
				}
			}

		}

		// -t
		options.arg_to_number<unsigned int>(THREADS_PER_TARGET, 0, &dummy.threads_per_target);

		// -T
		if (options.arg_to_number<off_t>(THREAD_STRIDE, dummy.block_size, &dummy.thread_offset)) {
			if (dummy.use_interlocked) {
				fprintf(stderr, "Stride between threads must be 0 if using -si\n");
				return false;
			}
		}

		// -v
		if (options.get_arg(VERBOSE)) {
			verbose = true;
		}
		// compiler flag for debugging
#ifdef ENABLE_DEBUG
		debug = true;
#endif

		// -w
		if (options.arg_to_number<unsigned int>(WRITE, 0, &dummy.write_percentage)) {
			if (dummy.write_percentage > 100) {
				fprintf(stderr, "Invalid argument. -w must be 0-100\n");
				return false;
			}
		}

		// -W
		options.arg_to_number<unsigned int>(WARMUP_TIME, 0, &job_options->warmup_time);

		// -x
		if (curr_arg = options.get_arg(IO_ENGINE)) {
			if (curr_arg[1] != '\0') {
				fprintf(stderr, "Invalid argument to -x\n");
				return false;
			}
			// initialize io engine
			switch(curr_arg[0]) {
				case 'k':
					job_options->io_manager = std::make_shared<KernelAsyncIOManager>();
					break;
				case 'p':
					job_options->io_manager = std::make_shared<PosixSuspendAsyncIOManager>();
					break;
				default:
					fprintf(stderr, "Invalid io engine specified. Choose from k, p\n");
                    return false;
			}
		} else {
			job_options->io_manager = std::make_shared<KernelAsyncIOManager>();
		}

		// -z
		if (curr_arg = options.get_arg(RAND_SEED)) {
			if (!curr_arg[0]) {
				// empty arg, use time
				job_options->use_time_seed = true;

			} else {
				// otherwise, set the seed
				options.arg_to_number<uint64_t>(RAND_SEED, 0, &job_options->rand_seed);
			}
		}

		// -Z
		if (curr_arg = options.get_arg(IO_BUFFERS)) {

			// look at each character in the arg string
			for (;curr_arg[0] != '\0'; curr_arg = &curr_arg[1]) {

				// set both and break
				if (curr_arg[0] == 's') {
					dummy.separate_buffers = true;
				} else if (curr_arg[0] == 'z') {
					dummy.zero_buffers = true;
				} else if (curr_arg[0] == 'r') {
					dummy.rand_buffers = true;
				} else {
					fprintf(stderr, "Invalid or unimplemented io-buffers argument -Z%c\n", curr_arg[0]);
					return false;
				}
			}
			if (dummy.rand_buffers && dummy.zero_buffers) {
				fprintf(stderr, "Conflicting arguments specified for -Z\n");
				return false;
			}
		}

		// now apply all the dummy options to the targets, and do createfile stuff
		for (auto& target : job_options->targets) {

			target->create_file			= dummy.create_file;
			target->block_size			= dummy.block_size;
			target->base_offset			= dummy.base_offset;

			target->overlap				= dummy.overlap;

			target->thread_offset		= dummy.thread_offset;
			target->stride				= dummy.stride;
			target->use_random_alignment= dummy.use_random_alignment;

			target->open_flags			= dummy.open_flags;

			target->use_interlocked		= dummy.use_interlocked;
			// set initial interlocked offset
			if (target->use_interlocked) {
				target->interlocked_offset = target->base_offset;
			}
			target->max_throughput		= dummy.max_throughput;

			target->threads_per_target	= dummy.threads_per_target;

			target->write_percentage	= dummy.write_percentage;

			target->zero_buffers		= dummy.zero_buffers;
			target->rand_buffers		= dummy.rand_buffers;
			target->separate_buffers	= dummy.separate_buffers;

			// add up the total threads, if -F wasn't specified
			if (!job_options->use_total_threads) {
				job_options->total_threads += target->threads_per_target;
			}

			// TODO move this createfile stuff to a function cos it'll be used by XML and JSON
			// stat the file
			struct stat buf = {0};
			int err = stat(target->path.c_str(), &buf);
			if (err && errno != ENOENT) {
				fprintf(stderr, "Unexpected error when statting target!\n");
#ifdef ENABLE_DEBUG
				perror("stat");
#endif
				return false;
			}
	
			if (!target->create_file) {
				// if the file doesn't exist, error out
				if (err) {
					fprintf(stderr, "Target \"%s\" does not exist!\n", target->path.c_str());
					return false;
				}

				// if it's a device, we need to get the size through sysfs
				if (buf.st_rdev) {
					target->size = sys_info->partition_size(buf.st_rdev);
				} else {
					target->size = buf.st_size;
				}

			} else {
				// create option shouldn't be used when specifying an existing device, so error
				if (buf.st_rdev) {
					fprintf(stderr, "Target \"%s\" is an existing device! Don't use -c!\n", target->path.c_str());
					return false;
				}

				// if the file already exists and is larger than the size we need, don't create
				if (!err && buf.st_size >= dummy.size) {
					target->create_file = false;
				}

				// use the size we got from -c regardless of whether we'll be re-creating the file
				target->size = dummy.size;
			}

			// default max_size to size if the option wasn't specified
			if (!dummy.max_size) {
				target->max_size = target->size;
			} else {
				target->max_size = dummy.max_size;
			}

			if (target->max_size > target->size) {
				fprintf(stderr, "target-size can't be larger than its actual size!\n");
				return false;
			}

			// check the block size against the max_size of the file
			if ((target->max_size - target->base_offset) < target->block_size ||
					target->max_size <= target->base_offset) {
				fprintf(stderr, "target %s's specified size is too small for block size of %lu bytes\n",
						target->path.c_str(),
						target->block_size);
				return false;
			}
		}

		// set the result formatter
		result_formatter = std::make_shared<ResultFormatterText>();

		// tell the job who its daddy is
		job_options->profile = this;

		job_options->sys_info = sys_info;

		// add the job to the list, passing the options in the constructor
		jobs.push_back(std::make_shared<Job>(job_options));

		return true;
	}

	bool Profile::run_jobs() {
		unsigned i = 0;
		for (auto& job : jobs) {
			if (!job->run_job()) {
				fprintf(stderr, "Job %u failed, exiting\n", i);
				return false;
			}
			++i;
		}
		return true;
	}

	void Profile::get_results() {
		result_formatter->output_results(*this);
	}
} // namespace
