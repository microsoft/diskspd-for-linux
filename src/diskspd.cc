// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "profile.h"

int main(int argc, char ** argv) {

	diskspd::Profile profile;

	// initialize profile with the command line options
	if (!profile.parse_options(argc, argv)) {
		return 1;
	}

	// run the profile
	if (!profile.run_jobs()) {
		return 1;
	}

	// output the results
	profile.get_results();

	return 0;
}
