// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <memory>
#include <utility>
#include <cstdio>
#include <assert.h>
#include <inttypes.h>

#include "result_formatter.h"
#include "profile.h"
#include "job.h"
#include "sys_info.h"

namespace diskspd {

	void ResultFormatterText::output_results(const Profile& profile) {
		printf("\nCommand Line: %s\n\n", profile.cmd_line.c_str());

		printf("System info:\n");
		printf("\tprocessor count: %lu\n", profile.sys_info->online_cpus.size());
		if (profile.sys_info->caching_options.size()) {
			printf("\tcaching options: %s\n", profile.sys_info->caching_options.c_str());
		}
		printf("\n");

		unsigned int jobnum = 1;
		for (auto& job : profile.jobs) {
			std::shared_ptr<JobOptions> options = job->get_options();
			std::shared_ptr<JobResults> results = job->get_results();
			printf ("Input parameters:\n\n");
			printf ("\tjob:   %u\n", jobnum);
			printf ("\t________\n");
			printf ("\tduration: %us\n", options->duration);
			printf ("\twarm up time: %us\n", options->warmup_time);
			if (options->measure_latency) {
				printf("\tmeasuring latency\n");
			}
			if (options->measure_iops_std_dev) {
				printf ("\tgathering IOPs at intervals of %ums\n", options->io_bucket_duration_ms);
			}
			if (options->use_time_seed) {
				printf("\t using random_device for seed\n");
			} else {
				printf ("\trandom seed: %lu\n", options->rand_seed);
			}

			unsigned int all_threads = options->use_total_threads ? options->total_threads : 0;
			if (!all_threads) {
				for (auto& target : options->targets) all_threads += target->threads_per_target;
			}

			printf("\ttotal threads: %u\n", all_threads);

			for (auto& target : options->targets) {
				printf("\tpath: '%s'\n", target->path.c_str());
				printf("\t\tsize: %luB\n", target->size);
				if (target->open_flags & O_DIRECT) {
					printf("\t\tusing O_DIRECT\n");
				}
				if (target->open_flags & O_SYNC) {
					printf("\t\tusing O_SYNC\n");
				}
				printf("\t\tperforming mix test (read/write ratio: %u/%u)\n",
						100-target->write_percentage, target->write_percentage);
				printf("\t\tblock size: %lu\n", target->block_size);
				if (target->use_random_alignment) {
					printf("\t\tusing random I/O (alignment: %lu)\n", target->stride);
				} else if (target->use_interlocked) {
					printf("\t\tusing interlocked sequential I/O (stride: %lu)\n", target->stride);
				} else {
					printf("\t\tusing sequential I/O (stride: %lu)\n", target->stride);
				}
				printf("\t\tnumber of outstanding I/O operations: %u)\n", target->overlap);
				if (target->base_offset) {
					printf("\t\tbase file offset: %lu bytes\n", target->base_offset);
				}
				if (target->max_size != target->size) {
					printf("\t\tmax file size: %lu bytes\n", target->max_size);
				}
				printf("\t\tthread stride size: %lu\n", target->thread_offset);
				if (target->zero_buffers) {
					printf("\t\tzeroing I/O buffers\n");
				} else if (target->rand_buffers) {
					printf("\t\tfilling I/O buffers with random data\n");
				}
				if (target->separate_buffers) {
					printf("\t\tseparating read and write buffers\n");
				}
				if (!options->use_total_threads) {
					printf("\t\tthreads per file: %u\n", target->threads_per_target);
				}
				printf("\t\tblock device: %s\n", target->device.c_str());
				printf("\t\tdevice scheduler: %s\n", target->scheduler.c_str());
			}
			printf("\n");

			printf("Results for job %u:\n", jobnum);
			++jobnum;

			printf("test time:         %us\n", options->duration);

			printf("*****************************************************\n\n");
	
			// CPU stats
            unsigned num_cpu_fields = results->cpu_usage_percentages.begin()->second.size();
			double cpu_usage_totals[num_cpu_fields] = {0};
			printf(" CPU  |  Usage  |   User  |  Kernel | IO Wait |   Idle \n");
			printf("-------------------------------------------------------\n");
			for (auto& usage : results->cpu_usage_percentages) {
				printf("%5u ", usage.first);
				// add to the totals and print the usage for each column
				for (int i = 0; i < num_cpu_fields; ++i) {
					double u = usage.second[i]*100;
					cpu_usage_totals[i] += u;
					printf("| %6.2lf%% ", u);
				}
				printf("\n");
			}
			printf("-------------------------------------------------------\n");
			printf(" avg:	");
			// calculate averages from totals and print one at a time
			for (int i = 0; i < num_cpu_fields; ++i) {
				double u = cpu_usage_totals[i]/profile.sys_info->online_cpus.size();
				printf("%6.2lf%%%s", u, (i == num_cpu_fields-1 ? "\n" : " | "));
			}
			printf("\n");

			/* *************************** IOPs **************************** */

			printf("Total IO\n");
			print_iops(job, RW);

			printf("Read IO\n");
			print_iops(job, READ);

			printf("Write IO\n");
			print_iops(job, WRITE);

			printf("\n");

			/* *************************** Latency %-iles **************************** */

			if (!options->measure_latency) return;
			// create histograms accumulating all reads and write
			Histogram<uint64_t> read_histogram;
			Histogram<uint64_t> write_histogram;
			Histogram<uint64_t> total_histogram;

			for (auto& thread_result : results->thread_results) {
				for (auto& t_result : thread_result->target_results) {
					read_histogram.Merge(t_result->read_latency_histogram);
					write_histogram.Merge(t_result->write_latency_histogram);

					total_histogram.Merge(read_histogram);
					total_histogram.Merge(write_histogram);
				}
			}

			// make sure there are reads/writes to get
			bool has_reads = read_histogram.GetSampleSize() > 0;
			bool has_writes = write_histogram.GetSampleSize() > 0;

			// buffers for snprintf
			char readbuf[11] = {0};
			char writebuf[11] = {0};
			char * na = (char*)"N/A";

			// pointers that either point to the buffer or na at any given time
			char * readstr = na;
			char * writestr = na;

			printf("  %%-ile |	Read (ms) | Write (ms) | Total (ms)\n");
			printf("----------------------------------------------\n");

			readstr = has_reads ?
				snprintf(readbuf, 11, "%10.3lf", (double)read_histogram.GetMin()/1000), readbuf :
				na;

			writestr = has_writes ?
				snprintf(writebuf, 11, "%10.3lf", (double)write_histogram.GetMin()/1000), writebuf :
				na;


			printf("    min | %10s | %10s | %10.3lf\n",
					readstr,
					writestr,
					(double)total_histogram.GetMin()/1000
				  );

			std::pair<double, std::string> percentiles[] = {
				{		0.25, "25th"	},
				{		0.50, "50th"	},
				{		0.75, "75th"	},
				{		0.90, "90th"	},
				{		0.95, "95th"	},
				{		0.99, "99th"	},
				{	   0.999, "3-nines" },
				{	  0.9999, "4-nines" },
				{	 0.99999, "5-nines" },
				{	0.999999, "6-nines" },
				{  0.9999999, "7-nines" },
				{ 0.99999999, "8-nines" },
				{ 0.999999999, "9-nines"}
			};

			for (auto& p : percentiles)
			{

				readstr = has_reads ?
					snprintf(
							readbuf,
							11,
							"%10.3lf",
							(double)read_histogram.GetPercentile(p.first)/1000
							), readbuf :
					na;

				writestr = has_writes ?
					snprintf(
							writebuf,
							11,
							"%10.3lf",
							(double)write_histogram.GetPercentile(p.first)/1000
							), writebuf :
					na;

				printf("%7s | %10s | %10s | %10.3lf\n",
						p.second.c_str(),
						readstr,
						writestr,
						(double)total_histogram.GetPercentile(p.first)/1000
					  );
			}

			readstr = has_reads ?
				snprintf(readbuf, 11, "%10.3lf", (double)read_histogram.GetMax()/1000), readbuf :
				na;

			writestr = has_writes ?
				snprintf(writebuf, 11, "%10.3lf", (double)write_histogram.GetMax()/1000), writebuf :
				na;

			printf("    max | %10s | %10s | %10.3lf\n",
					readstr,
					writestr,
					(double)total_histogram.GetMax()/1000
				  );

			printf("\n");
		}
	}

// macro for printing a bar in the print_iops function
#define IOPS_RESULT_BAR() { \
	printf("-------------------------------------------------------------------------------"); \
	if (options->measure_iops_std_dev) {		\
		printf("------------");				   \
	}											\
	if (options->measure_latency) {				\
		printf("------------------------");   \
	}											\
	printf("\n");								\
}

// macro for getting appropriate read/write values in the print_iops function
#define IOPS_GET(rval, wval, rwval) (t == READ ? t_result->rval : t == WRITE ? t_result->wval : t_result->rwval)

	void ResultFormatterText::print_iops(const std::shared_ptr<Job>& job, Type t) {

		std::shared_ptr<JobOptions> options = job->get_options();
		std::shared_ptr<JobResults> results = job->get_results();

		printf("thread |           bytes |         I/Os |       MB/s |  I/O per s |%s%s%s file\n",
				options->measure_iops_std_dev ? " IopsStdDev |" : "",
				options->measure_latency ?		" AvgLat(ms) |" : "",
				options->measure_latency ?		" LatStdDev  |" : "");

		IOPS_RESULT_BAR()

		double bucket_time_seconds = (double)options->io_bucket_duration_ms/1000.0;

		uint64_t total_bytes = 0;
		uint64_t total_iops = 0;
		IoBucketizer total_bucketizer;
		Histogram<uint64_t> total_histogram;

		for (auto& thread_result : results->thread_results) {

			for (auto& t_result : thread_result->target_results) {

				printf("%6d | %15lu | %12lu | %10.2lf | %10.2lf ",
						thread_result->thread_id,
						IOPS_GET(read_bytes_count, write_bytes_count, bytes_count),
						IOPS_GET(read_iops_count, write_iops_count, iops_count),
						(double)IOPS_GET(
							read_bytes_count,
							write_bytes_count,
							bytes_count)/ (1<<20) / (double)options->duration,
						(double)IOPS_GET(
							read_iops_count,
							write_iops_count,
							iops_count)/ (double)options->duration
					  );

				// iops stddev
				if (options->measure_iops_std_dev) {
					// make a new bucketizer just for this scope
					IoBucketizer curr_bucketizer;

					// merge with read, write or both (makes this section a tad cleaner)
					if (t == READ || t == RW) {
						curr_bucketizer.Merge(t_result->read_bucketizer);
					}
					if (t == WRITE || t == RW) {
						curr_bucketizer.Merge(t_result->write_bucketizer);
					}
					// merge with the total (for the final row)
					total_bucketizer.Merge(curr_bucketizer);
					// print this row
					printf("| %10.2lf ",
							curr_bucketizer.GetStandardDeviation()/bucket_time_seconds);
				}

				// latency statistics
				if (options->measure_latency) {
					// same idea as the iops above
					Histogram<uint64_t> curr_histogram;

					if (t == READ || t == RW) {
						curr_histogram.Merge(t_result->read_latency_histogram);
					}
					if (t == WRITE || t == RW) {
						curr_histogram.Merge(t_result->write_latency_histogram);
					}

					total_histogram.Merge(curr_histogram);

					// us -> ms
					printf("|    %8.3lf ",
							(double)curr_histogram.GetAvg()/1000.0);

					// avoid nans
					if (curr_histogram.GetSampleSize() > 0) {
						printf("|    %8.3lf ",
							  (double)curr_histogram.GetStdDev()/1000.0);
					} else {
						printf("|       N/A ");
					}
				}

				total_bytes += IOPS_GET(read_bytes_count, write_bytes_count, bytes_count);
				total_iops	+= IOPS_GET(read_iops_count, write_iops_count, iops_count);

				printf("| %s (%lu%s)",
						t_result->target->path.c_str(),
						t_result->target->size,
						"B");
				printf("\n");
			}
		}

		IOPS_RESULT_BAR()

			printf("total:   %15lu | %12lu | %10.2lf | %10.2lf ",
					total_bytes,
					total_iops,
					(double)total_bytes / (1<<20) / (double)options->duration,
					(double)total_iops / (double)options->duration);
		// total iops std dev
		if (options->measure_iops_std_dev) {
			printf("| %10.2lf ", total_bucketizer.GetStandardDeviation()/bucket_time_seconds);
		}
		// total avg lat, std dev
		if (options->measure_latency) {

			// us -> ms
			printf("|    %8.3lf ", (double)total_histogram.GetAvg()/1000.0);

			// avoid nans
			if (total_histogram.GetSampleSize() > 0) {
				printf("|    %8.3lf ", (double)total_histogram.GetStdDev()/1000.0);
			} else {
				printf("|	   N/A ");
			}
		}
		printf("\n\n");
	}
}
