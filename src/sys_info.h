// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <cstdint>
#include <vector>
#include <set>
#include <map>

#ifndef DISKSPD_SYS_INFO_H
#define DISKSPD_SYS_INFO_H

namespace diskspd {

	struct SysInfo {

		unsigned int cpulo = 0;
		unsigned int cpuhi = 0;

		std::set<unsigned int> online_cpus = {0};
		std::set<unsigned int> affinity_cpus = {0};

		/**
		 *	Populate the struct with relevant system info
		 *	Optionally, provide a string which describes a set of cpus for the affinity_cpus set
		 */
		void init_system_info(const char * affinity_set);

		/**
		 *	Parse the contents of /proc/stat
		 *	Returned keys are the cpu ids
		 *	Returned values are 4 element vectors pertaining to time that cpu has spent in:
		 *	user mode, user mode with low priority (nice), system (kernel), idle
		 */
		std::map<unsigned int, std::vector<double> > get_cpu_stats();

		// for debugging
		std::string print_sys_info();

		private:

			/**
			 *	parse the string to figure out which cpus are online
			 *	the format is like 0-7,9,12,32-36 i.e ranges of numbers separated by commas
			 */
			static std::set<unsigned int> str_to_cpu_set(const char * s);

	};

}

#endif // DISKSPD_SYS_INFO_H
