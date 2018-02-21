// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <iostream>
#include <fstream>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <linux/limits.h>	// PATH_MAX
#include <unistd.h>			// readlink
#include <dirent.h>			// dirent
#include <libgen.h>			// basename
#include <sys/types.h>		// DIR?
#include <sys/stat.h>		// stat

#include "debug.h"
#include "sys_info.h"

namespace diskspd {

	std::set<unsigned int> SysInfo::str_to_cpu_set(const char * s) {

		std::set<unsigned int> ret;

		if (!s) return ret;

		// pointer into the string
		const char * ptr = const_cast<char*>(s);

		// fields for denoting a range of cpus
		unsigned int curr, next;

		char * endptr;

		// we'll loop as long as there are fields left denoting active cpus (comma separated)
		while (1) {

			// stop if we reach an invalid field
			if (*ptr < '0' || *ptr > '9') break;

			// get an integer
			curr = strtoul(ptr, &endptr, 10);

			// check for strtoul error
			if (curr == std::numeric_limits<unsigned int>::max()) {
				fprintf(stderr, "Invalid cpu set \"%s\"", s);
				exit(1);
			}

			// advance past this number
			ptr = endptr;

			// if there's a dash, it means this field specifies a range of cpus
			if (*ptr == '-') {
				// get the next number after the '-' and advance past it
				++ptr;

				// stop if we reach an invalid field
				if (*ptr < '0' || *ptr > '9') break;

				next = strtoul(ptr, &endptr, 10);

				// check for strtoul error
				if (next == std::numeric_limits<unsigned int>::max()) {
					fprintf(stderr, "Invalid cpu set \"%s\"", s);
					exit(1);
				}

				// advance past this number
				ptr = endptr;

			// otherwise, only one cpu in this field
			} else {
				next = curr;
			}

			// insert the range of cpu numbers [curr,next] into the set
			for (int i = curr; i <= next; ++i) {
				ret.insert(i);
			}

			// we're done if there are no more number fields (which are ',' separated)
			if (*ptr != ',') break;

			// otherwise increment the pointer past the comma to the next field
			++ptr;
		}

		return ret;
	}

	void SysInfo::init_sys_info(const char * affinity_set) {

		// Prevent the (perceived) cpu topology from being changed at runtime
		static bool called = false;
		if (called) {
			return;
		}
		called = true;

		std::string line;
		std::ifstream onlinefile("/sys/devices/system/cpu/online");

		if (!onlinefile.is_open()) {
			fprintf(stderr, "Error opening /sys/devices/system/cpu/online\n");
			exit(1);
		}

		std::getline(onlinefile,line);

		onlinefile.close();

		online_cpus = str_to_cpu_set(line.c_str());

		// Get lowest and highest cpu numbers
		cpulo = *online_cpus.begin();
		cpuhi = *online_cpus.rbegin();

		if (affinity_set) {
			affinity_cpus = str_to_cpu_set(affinity_set);
		} else {
			affinity_cpus = online_cpus;
		}
		
		// ************* block device stuff ****************

		// all block devices are listed here, including partitions
		DIR * c_b_dir = opendir("/sys/class/block");
		if (c_b_dir == nullptr) {
			fprintf(stderr, "Failed to get list of block devices from /sys/class/block");
			perror("opendir");
			exit(1);
		}

		struct dirent* result = nullptr;
		struct dirent c_b_dirent = {0};
		int err;
		std::string temp;
		std::set<std::string> block_devices;

		// loop through the directory stream until we reach the end
		while (err = readdir_r(c_b_dir, &c_b_dirent, &result), result != nullptr) {

			if (err) { // only error is EBADF for c_b_dir, realllly unlikely
				fprintf(stderr, "Error in device info initialization\n");
				perror("readdir_r");
				exit(1);
			}
			temp = std::string(c_b_dirent.d_name);
			// ignore hidden files
			if (temp[0] == '.') continue;
			block_devices.insert(temp);
		}

		std::string dev_path;
		struct stat dev_stat = {0};

		// look each partition up in /dev, compare device_id until we find it
		for (auto& str : block_devices) {
			dev_path = "/dev/" + str;
			int err = stat(dev_path.c_str(), &dev_stat);
			if (err) {
				fprintf(stderr, "Unexpected error when statting device!\n");
				perror("stat");
				exit(1);
			}
			//printf("mapping %u,%u to %s\n", major(dev_stat.st_rdev), minor(dev_stat.st_rdev), str.c_str());
			// map it for later!
			id_to_device[dev_stat.st_rdev] = str;
		}

		// ************* fua caching ****************

		std::ifstream fuafile("/sys/module/libata/parameters/fua");

		// if we can't open it, the kernel probably doesn't know the state of fua, so ignore
		if (fuafile.is_open()) {
			std::getline(fuafile,line);
			fuafile.close();
			caching_options = "fua="+line;
		}
	}

	std::map<unsigned int, std::vector<double> > SysInfo::get_cpu_stats() {

		std::map<unsigned int, std::vector<double> > stats;

		std::string line;
		std::ifstream statfile("/proc/stat");

		if (!statfile.is_open()) {
			fprintf(stderr, "Error opening /proc/stat\n");
			exit(1);
		}

		// ignore first line (total cpu time)
		std::getline(statfile,line);

		// get cpu info for each cpu
		int found = 0;
		unsigned int actual_cpu;
		double temp[5];
		for (auto& expected_cpu : online_cpus) {

			if (!std::getline(statfile,line)) {
				fprintf(stderr, "Error reading /proc/stat for cpu %d\n", expected_cpu);
				exit(1);
			}

			found = sscanf(
					line.c_str(),
					"cpu%u %lf %lf %lf %lf %lf",
					&actual_cpu,
					&temp[0], &temp[1], &temp[2], &temp[3], &temp[4]);
			//printf("iowait: %lf\n", temp[4]);

			// check result is what we expected, in case cpu topology has changed at runtime
			if (actual_cpu != expected_cpu || found != 6) {
				fprintf(stderr, "Discrepancy reading /proc/stat for cpu%d\n", expected_cpu);
				exit(1);
			}

			// update map
			stats[actual_cpu].resize(5);
			for (int i = 0; i < 5; ++i) {
				stats[actual_cpu][i] = temp[i];
			}
		}

		statfile.close();

		return stats;
	}
	
	std::string SysInfo::print_sys_info() {
		std::string result =
			"total cpus: "+std::to_string(online_cpus.size())+
			"\nlowest cpu id: "+std::to_string(cpulo)+
			"\nhighest cpu id: "+std::to_string(cpuhi)+
			"\nall available cpus: \n";
		for (auto& i : online_cpus) {
			result += std::to_string(i) + " ";
		}
		result+="\n";
		return result;
	}


	std::string SysInfo::device_from_id(dev_t device_id) {
		// this looks at the symlink in sys/class/block/$dev
		// the symlink here ends with "block/$devname/$partitionname" or "block/$devname"
		// in either case, we get the 'up one' name, ie $devname or "block"
		// if we get someting that isn't "block", then that's the actual device. otherwise, if it's
		// "block", we know that the original name is the device name, so just return that
		
		// this shouldn't happen if sys-info's been initialized properly
		if (!id_to_device.count(device_id)) {
			fprintf(
				stderr,
				"Tried to lookup nonexistent device %u,%u in sys_info!\n",
				major(device_id),
				minor(device_id));
			exit(1);
		}

		std::string dev_name = id_to_device[device_id];
	
		std::string linkpath = "/sys/class/block/" + dev_name;
		char the_link[PATH_MAX+1] = {0};
		ssize_t link_size = readlink(linkpath.c_str(), the_link, PATH_MAX);
		if (link_size == -1) {
			fprintf(stderr, "Error reading link!\n");
			perror("readlink");
			exit(1);
		}
		// add null terminator; readlink has few guarantees
		the_link[link_size] = '\0';

		// these use statically allocated memory, or modify the original path. don't free them
		char * up_one = dirname(the_link);			// "abc/abc/sda/sda1" -> "abc/abc/sda"
		char * up_one_name = basename(up_one);		// "abc/abc/sda" -> "sda"

		// if the device is not a partition, then the 'one up' path is 'block' instead
		if (strncmp(up_one_name, "block", 5) == 0) {
			return dev_name;	// so just return the original name we had
		}

		return std::string(up_one_name);
	}

	std::string SysInfo::scheduler_from_device(std::string device) {

		std::string line;
		std::ifstream schedfile(std::string("/sys/block/"+device+"/queue/scheduler"));

		if (!schedfile.is_open()) {
			fprintf(stderr, "Couldn't open scheduler file for device %s\n", device.c_str());
			exit(1);
		}

		std::getline(schedfile, line);

		schedfile.close();
		
		// the line of schedulers is like "sched1 sched2 [selectedsched]"
		char buf[line.size() + 1];
		buf[line.size()] = '\0';
		memcpy(buf, line.c_str(), line.size());

		ssize_t schedstart = -1;
		for (unsigned i = 0; i < line.size(); ++i) {
			if (buf[i] == '[') {
				schedstart = i+1;
			}
			if (buf[i] == ']') {
				buf[i] = '\0';
				break;
			}
		}

		std::string sched = std::string(&buf[schedstart]);

		return sched;
	}

	off_t SysInfo::partition_size(dev_t device_id) {
		if (!id_to_device.count(device_id)) {
			fprintf(
					stderr,
					"Tried to lookup nonexistent device %u,%u in sys_info!\n",
					major(device_id),
					minor(device_id));
			exit(1);
		}

		std::string line;
		std::ifstream sizefile("/sys/class/block/" + id_to_device[device_id] + "/size");

		if (!sizefile.is_open()) {
			fprintf(stderr, "Couldn't open size file for device\n");
			exit(1);
		}

		std::getline(sizefile, line);

		sizefile.close();
		
		unsigned long long sz = strtoull(line.c_str(), NULL, 10);
		sz *= 512;	// TODO is this sector size or always 512?
		return static_cast<off_t>(sz);
	}
}
