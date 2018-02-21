// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include<vector>
#include<string>
#include<map>
#include<argp.h>

#ifndef DISKSPD_OPTIONS_H
#define DISKSPD_OPTIONS_H


namespace diskspd {

	enum OptionType {
		CPU_AFFINITY,
		BLOCK_SIZE,
		BASE_OFFSET,
		CREATE_FILES,
		DURATION,
		DIOPS,
		MAX_SIZE,
		TOTAL_THREADS,
		MAX_THROUGHPUT,
		LATENCY,
		NO_AFFINITY,
		OVERLAP,
		RANDOM_ALIGN,
		SEQUENTIAL_STRIDE,
		CACHING_OPTIONS,
		THREADS_PER_TARGET,
		THREAD_STRIDE,
		VERBOSE,
		IO_ENGINE,
		WRITE,
		WARMUP_TIME,
		RAND_SEED,
		IO_BUFFERS
	};

	/**
	 *	Generic tools for parsing options
	 */
	class Options {

		public:

			/**
			 *	Parse the command line arguments, creating a map of OptionTypes, and vector of
			 *	non-option arguments
			 */
			bool parse_args(int argc, char ** argv);

			/**
			 *	Parse a single argument and add it to the map or vector
			 */
			error_t parse_arg(int key, char *arg);

			/**
			 *	Get an argument if it was parsed. return null if not
			 */
			inline const char * get_arg(OptionType o) { opts.count(o) ? opt_map[opts[o]].arg.c_str() : nullptr; }

			/**
			 *	Get the vector of non-option arguments
			 */
			inline std::vector<std::string> get_non_opts() { return non_opts; }

			/**
			 *	Check if an argument is (entirely) numeric
			 */
			static bool is_numeric(const char *arg);

			/**
			 *	Check if an argument conforms to the format required for arguments with a number and size
			 *	specifier
			 */
			static bool valid_byte_size(const char *arg);

			/**
			 *	Return number of bytes to multiply an argument by based on the provided size specifier
			 */
			static unsigned long long getSizeMultiplier(char specifier, size_t block_size);

			static unsigned long long byte_size_from_arg(const char * curr_arg, size_t block_size);

			/**
			 *	Convert an argument into a number. If it's an option with the OPT_BYTE_SIZE flag,
			 *	it	will apply the multiplier (K|M|G|b) in the string if there is one, using
			 *	block_size for "b".
			 *	If the specified option wasn't in the list of args, it returns false without doing
			 *	anything.
			 *	If an option is specified that can't be converted to the type of ret, error out.
			 */
			template <class T>
			bool arg_to_number(OptionType opt, size_t block_size, T * ret) {
				// fail if option wasn't given
				if (!opts.count(opt)) return false;

				// get the option
				DiskspdOption& option = opt_map[opts[opt]];

				// check there's an actual arg, if not, succeed but do nothing
				if (!option.arg.c_str()[0]) return true;

				unsigned long long result;
				// check if it's a number
				if (option.flags & OPT_NUMERIC) {
					result = strtoull(option.arg.c_str(), NULL, 10);
				// check if it's a byte size
				} else if (option.flags & OPT_BYTE_SIZE) {
					result = byte_size_from_arg(option.arg.c_str(), block_size);
				} else {
					assert(!"Invalid argument passed to arg_to_number!"); // programmer error
				}
				if (result > std::numeric_limits<T>::max()) {
					fprintf(stderr, "Argument to -%c too large\n", (char)option.opt.key);
					exit(1);
				}
				*ret = static_cast<T>(result);

				return true;
			}

		private:
			// map of OptionType enum to the integer 'key' of the option in the opt_map
			std::map<OptionType, int> opts;
			std::vector<std::string> non_opts;

			// diskspd usage string
			const char * non_opt_doc = "FILE [FILE...]";
			// diskspd doc string
			const char * docstring =
				"Disk I/O benchmarking tool. Specify desired options followed by name(s) of at "
				"least one file or disk to do I/O on. Do not leave spaces between an option and "
				"argument\n"
				"NOTE: If you are familiar with the Windows version of this tool, note that some "
				"options may behave differently or have slightly different defaults. \v"
				"By Nuno Das Neves (t-nudasn at microsoft dot com)";

			const uint32_t OPT_NUMERIC		= 0x1;
			const uint32_t OPT_BYTE_SIZE	= 0x2;
			const uint32_t OPT_NON_ZERO		= 0x4;

			struct DiskspdOption {
				OptionType type;		// enum describing which option this represents
				uint32_t flags;		// flags for diskspd not covered by default by argp
				std::string arg;		// where the arg will be copied to
				argp_option opt;		// the argp_option for argparse
			};

			// map of keys to argparse options and OptionType enums
			// the 'arg' string in each value is updated by the argp option parser
			std::map<int, DiskspdOption> opt_map = {
					{
						(int)'a',
						{
							type: CPU_AFFINITY,
							flags: 0,
							arg: "",
							opt:
							{
								name:"cpu-affinity",
								key:(int)'a',
								arg:"CPU_SET",
								flags:0,
								doc:
									"By default, threads are affinitized round-robin across "
									"all online cpus in the system. Use this option to limit "
									"the cpus used by providing a cpu set as an argument. "
									"Use comma-delimited groups of cpus ids to specify a set "
									"e.g. \"-a 0-3,7\" = cpus 0,1,2,3,7",
								group:0
							}
						}
					},
					{
						(int)'b',
						{
							type: BLOCK_SIZE,
							flags: OPT_BYTE_SIZE | OPT_NON_ZERO,
							arg: "",
							opt:
							{
								name:"block-size",
								key:(int)'b',
								arg:"BLOCK_SIZE[K|M|G]",
								flags:0,
								doc:
									"Block size in bytes or KiB(K), MiB(M), or GiB(G) "
									"(default=64K)\n",
								group:0
							}
						}
					},
					{
						(int)'B',
						{
							type: BASE_OFFSET,
							flags: OPT_BYTE_SIZE,
							arg: "",
							opt:
							{
								name:"base-offset",
								key:(int)'B',
								arg:"BASE_OFFSET[K|M|G|b]",
								flags:0,
								doc:
									"Base target offset in bytes or KiB(K), MiB(M), GiB(G), or "
									"blocks(b) from the beginning of the target (default=0). Must "
									"be less than target-size (-f). "
									"i.e. areas of the file outside the interval "
									"[base-offset,target-size) will not be touched.\n",
								group:0
							}
						}
					},
					{
						(int)'c',
						{
							type: CREATE_FILES,
							flags: OPT_BYTE_SIZE | OPT_NON_ZERO,
							arg: "",
							opt:
							{
								name:"create-files",
								key:(int)'c',
								arg:"FILE_SIZE[K|M|G|b]",
								flags:0,
								doc:
									"Create files of the specified size in bytes or KiB(K), "
									"MiB(M), GiB(G), or blocks(b).\n",
								group:0
							}
						}
					},
					{
						(int)'d',
						{
							type: DURATION,
							flags:OPT_NUMERIC | OPT_NON_ZERO,
							arg: "",
							opt:
							{
								name:"duration",
								key:(int)'d',
								arg:"DURATION",
								flags:0,
								doc:
									"Duration of measurement period in seconds, not including "
									"cooldown or warmup time (default=10)\n",
								group:0
							}
						}
					},
					{
						(int)'D',
						{
							type: DIOPS,
							flags:OPT_NUMERIC,
							arg: "",
							opt:
							{
								name:"iops-std-dev",
								key:(int)'D',
								arg:"INTERVAL",
								flags:OPTION_ARG_OPTIONAL,
								doc:
									"Calculate IOPs standard deviation, and specify millisecond "
									"intervals for bucketing IOPs data. These are per-thread "
									"per-target. You can specify this without an argument and it "
									"will default to 1000 ms or 1 sec.\n",
								group:0
							}
						}
					},
					{
						(int)'f',
						{
							type: MAX_SIZE,
							flags: OPT_BYTE_SIZE | OPT_NON_ZERO,
							arg: "",
							opt:
							{
								name:"target-size",
								key:(int)'f',
								arg:"MAX_FILE_SIZE[K|M|G|b]",
								flags:0,
								doc:
									"Use only the first <arg> bytes or KiB(K), MiB(M), GiB(G) or "
									"blocks(b) of the specified targets, for example to test only "
									"the first sectors of a disk. Must be greater than base "
									"offset (-B). i.e. areas of the file outside the interval "
									"[base-offset,target-size) will not be touched.\n",
								group:0
							}
						}
					},
					{
						(int)'F',
						{
							type: TOTAL_THREADS,
							flags: OPT_NUMERIC,
							arg: "",
							opt:
							{
								name:"total-threads",
								key:(int)'F',
								arg:"TOTAL_THREADS",
								flags:0,
								doc:
									"Total number of threads. Conflicts with -t, the option to set "
									"the number of threads per file.\n",
								group:0
							}
						}
					},
					{
						(int)'g',
						{
							type: MAX_THROUGHPUT,
							flags: OPT_BYTE_SIZE | OPT_NON_ZERO,
							arg: "",
							opt:
							{
								name:"throttle-throughput",
								key:(int)'g',
								arg:"THROUGHPUT_PER_MS[K|M|G|b]",
								flags:0,
								doc:
									"Throughput per-thread per-target is throttled to the given "
									"number of bytes, KiB(K), MiB(M), GiB(G) or blocks(b) per "
									"millisecond. NOTE: this option has varying accuracy depending "
									"on number of threads (-t/-F) and cpu usage.\n",
								group:0
							}
						}
					},
					{
						(int)'L',
						{
							type: LATENCY,
							flags: 0,
							arg: "",
							opt:
							{
								name:"latency",
								key:(int)'L',
								arg:nullptr,
								flags:0,
								doc:
									"Measure latency statistics - avg latency of IOPS per-thread "
									"per-target, and standard deviation.\n",
								group:0
							}
						}
					},
					{
						(int)'n',
						{
							type: NO_AFFINITY,
							flags: 0,
							arg: "",
							opt:
							{
								name:"no-affinity",
								key:(int)'n',
								arg:nullptr,
								flags:0,
								doc:
									"Disable cpu affinity (default and -a).\n",
								group:0
							}
						}
					},
					{
						(int)'o',
						{
							type: OVERLAP,
							flags: OPT_NUMERIC | OPT_NON_ZERO,
							arg: "",
							opt:
							{
								name:"overlap",
								key:(int)'o',
								arg:"OVERLAP",
								flags:0,
								doc:
									"Number of outstanding I/O requests per-thread per-target. "
									"(default=2) Also known as io-depth.\n",
								group:0
							}
						}
					},
					{
						(int)'r',
						{
							type: RANDOM_ALIGN,
							flags: OPT_BYTE_SIZE | OPT_NON_ZERO,
							arg: "",
							opt:
							{
								name:"random-align",
								key:(int)'r',
								arg:"RANDOM_ALIGNMENT[K|M|G|b]",
								flags:OPTION_ARG_OPTIONAL,
								doc:
									"Random I/O aligned to the specified number of bytes or "
									"KiB(K), MiB(M), GiB(G), or blocks(b). Overrides -s. "
									"Omit the argument to align to block size by default.\n",
								group:0
							}
						}
					},
					{
						(int)'s',
						{
							type: SEQUENTIAL_STRIDE,
							flags: 0, // the [i] option has to be handled manually..
							arg: "",
							opt:
							{
								name:"sequential-stride",
								key:(int)'s',
								arg:"[i]STRIDE_SIZE[K|M|G|b]",
								flags:0,
								doc:
									"Sequential stride size, offset between "
									"subsequent I/O operations per-thread in bytes or KiB(K), "
									"MiB(M), GiB(G), or blocks(b). Ignored if -r specified "
									"(default access = sequential, default stride = block size)."
									"By default each thread tracks its own sequential offset. If "
									"the optional interlocked (i) qualifier is used, a single "
									"interlocked offset is shared between all threads operating "
									"on a given target so that the threads cooperatively issue a "
									"single sequential pattern of access to the target.\n",
								group:0
							}
						}
					},
					{
						(int)'S',
						{
							type: CACHING_OPTIONS,
							flags: 0,
							arg: "",
							opt:
							{
								name:"caching-options",
								key:(int)'S',
								arg:"[d|s|h]",
								flags:0,
								doc:
									"Modifies caching behavior for targets by "
									"altering the flags passed to open(). By default, no special "
									"flags are specified - i.e. caching is on. d = O_DIRECT flag; "
									"this disables caching for this file, but the device may still "
									"buffer requests. s = O_SYNC flag; write requests only return "
									"when data has been written to the underlying device. h = both "
									"O_DIRECT and O_SYNC are used.\n",
								group:0
							}
						}
					},
					{
						(int)'t',
						{
							type: THREADS_PER_TARGET,
							flags: OPT_NUMERIC | OPT_NON_ZERO,
							arg: "",
							opt:
							{
								name:"threads-per-target",
								key:(int)'t',
								arg:"THREADS_PER_TARGET",
								flags:0,
								doc:
									"Number of threads per target. Conflicts with -F, which "
									"specifies the total number of threads (default=1).\n",
								group:0
							}
						}
					},
					{
						(int)'T',
						{
							type: THREAD_STRIDE,
							flags: OPT_BYTE_SIZE,
							arg: "",
							opt:
							{
								name:"thread-stride",
								key:(int)'T',
								arg:"THREAD-STRIDE[K|M|G|b]",
								flags:0,
								doc:
									"Stride size between starting offsets of each thread operating "
									"on the same target in bytes or KiB(K), MiB(M), GiB(G), or "
									"blocks(b). (default = 0) The starting offset of a thread = base "
									"file offset + thread number * thread stride. Has no effect if "
									"there is only one thread per target.\n",
								group:0
							}
						}
					},
					{
						(int)'v',
						{
							type: VERBOSE,
							flags: 0,
							arg: "",
							opt:
							{
								name:"verbose",
								key:(int)'v',
								arg:nullptr,
								flags:0,
								doc:
									"Enable verbose mode - print out details of operations as they "
									"happen.\n",
								group:0
							}
						}
					},
					{
						(int)'w',
						{
							type: WRITE,
							flags: OPT_NUMERIC,
							arg: "",
							opt:
							{
								name:"write",
								key:(int)'w',
								arg:"WRITE_PERCENTAGE",
								flags:OPTION_ARG_OPTIONAL,
								doc:
									"Percentage of write requests to issue (default=0, i.e 100% "
									"read). The following are equivalent and result in a 100% "
									"read-only workload: omitting -w, specifying -w with no "
									"percentage, and -w0. IMPORTANT: a write test will destroy "
									"existing data without a warning.\n",
								group:0
							}
						}
					},
					{
						(int)'W',
						{
							type: WARMUP_TIME,
							flags: OPT_NUMERIC,
							arg: "",
							opt:
							{
								name:"warmup-time",
								key:(int)'W',
								arg:"WARMUP_TIME",
								flags:0,
								doc:
									"Duration in seconds to run the test before results start "
									"being recorded (default = 5 seconds).\n",
								group:0
							}
						}
					},
					{
						(int)'x',
						{
							type: IO_ENGINE,
							flags: 0,
							arg: "",
							opt:
							{
								name:"io-engine",
								key:(int)'x',
								arg:"[k|p]",
								flags:0,
								doc:
									"Which io engine to use. k = libaio (kernel "
									"aio interface), p = posix aio (userspace implementation) "
									"default=k\n",
								group:0
							}
						}
					},
					{
						(int)'z',
						{
							type: RAND_SEED,
							flags: OPT_NUMERIC,
							arg: "",
							opt:
							{
								name:"rand-seed",
								key:(int)'z',
								arg:"RAND_SEED",
								flags:OPTION_ARG_OPTIONAL,
								doc:
									"Set random seed to specified integer value. With no -z, "
									"seed=0. With plain -z, seed is based on C++'s random_device."
									"\n",
								group:0
							}
						}
					},
					{
						(int)'Z',
						{
							type: IO_BUFFERS,
							flags: 0,
							arg: "",
							opt:
							{
								name:"io-buffers",
								key:(int)'Z',
								arg:"[zrs]",
								flags:0,
								doc:
									"By default, buffers are shared between reads and writes, and "
									"contain a repeating pattern (0,1,2...255,0,1).\n"
									"z = zero the buffers instead. r = fill the buffers with "
									"random data. s = separate the read and write buffers. z and r "
									"conflict.\n",
									group:0
							}
						}
					}
			};
	};

} //namespace diskspd

#endif // DISKSPD_OPTIONS_H

