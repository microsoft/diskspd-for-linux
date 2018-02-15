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

	void SysInfo::init_system_info(const char * affinity_set) {

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

		// get lowest and highest cpu numbers
		cpulo = *online_cpus.begin();
		cpuhi = *online_cpus.rbegin();

		if (affinity_set) {
			affinity_cpus = str_to_cpu_set(affinity_set);
		} else {
			affinity_cpus = online_cpus;
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


}
