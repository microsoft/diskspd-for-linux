// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include<vector>
#include<string>
#include<memory>
#include<cstdio>
#include<cstdint>

#include "sys_info.h"
#include "job.h"
#include "result_formatter.h"
#include "target.h"
#include "thread.h"

#ifndef DISKSPD_PROFILE_H
#define DISKSPD_PROFILE_H

namespace diskspd {

	/**
	 *	Stores a representation of the entire run of diskspd
	 *	Initially populated with arguments by CmdLineParser
	 *	Updated by JobRunner as Jobs are run
	 *	Finally used by ResultParser to output results
	 */
	class Profile {

		/// The ResultFormatter needs to access most of Profile
		friend class IResultFormatter;
		friend class ResultFormatterText;

		public:

			/// record of what the user typed
			std::string cmd_line;

			/**
			 *	Parse command line options to populate this Profile with Jobs, system information
			 *	and a results formatter. This also sets the global verbose and debug flags
			 */
			bool parse_options(int argc, char ** argv);

			/**
			 *	Run the Jobs in this profile, one after the other.
			 */
			bool run_jobs();

			/**
			 *	Output the results from the Jobs using the result formatter.
			 */
			void get_results();

		private:

			/// Jobs to run
			std::vector<std::shared_ptr<Job>> jobs;

			/// An interface defining a class that formats and outputs the results of the profile
			std::shared_ptr<IResultFormatter> result_formatter;

			/// Info about the system (cpus etc)
			std::shared_ptr<SysInfo> sys_info;

	};

} //namespace diskspd

#endif // DISKSPD_PROFILE_H
