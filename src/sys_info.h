// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <cstdint>
#include <vector>
#include <set>
#include <map>
#include <sys/stat.h>

#ifndef DISKSPD_SYS_INFO_H
#define DISKSPD_SYS_INFO_H

namespace diskspd {

	struct SysInfo {

		unsigned int cpulo = 0;
		unsigned int cpuhi = 0;

		std::set<unsigned int> online_cpus = {0};
		std::set<unsigned int> affinity_cpus = {0};

		/**
		 *	Populate the the cpu related fields with relevant information, and also id_to_device map
		 *	Optionally, provide a string which describes a set of cpus for the affinity_cpus set
		 */
		void init_sys_info(const char * affinity_set);

		/**
		 *	Parse the contents of /proc/stat
		 *	Returned keys are the cpu ids
		 *	Returned values are 4 element vectors pertaining to time that cpu has spent in:
		 *	user mode, user mode with low priority (nice), system (kernel), idle
		 */
		std::map<unsigned int, std::vector<double> > get_cpu_stats();

		/**
		 *	Uses sysfs to determine the device name given a device id.
		 *	Gets the underlying device name
		 */
		std::string device_from_id(dev_t device_id);

		/**
		 *	Uses sysfs to get the scheduler the kernel is using for a given device
		 */
		std::string scheduler_from_device(std::string device);

		/**
		 *	Uses sysfs to get the size of a block device or partition given a device id
		 */
		off_t partition_size(dev_t device_id);


		// for debugging
		std::string print_sys_info();

		private:
			/**
			 *	parse the string to figure out which cpus are online
			 *	the format is like 0-7,9,12,32-36 i.e ranges of numbers separated by commas
			 */
			static std::set<unsigned int> str_to_cpu_set(const char * s);

			/**
			 *	device id's mapped to their device or parition name, initialized by init_sys_info
			 */
			std::map<dev_t, std::string> id_to_device;

	};

}

#endif // DISKSPD_SYS_INFO_H
