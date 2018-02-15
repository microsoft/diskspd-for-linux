// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include<cstdio>

#ifndef DISKSPD_DEBUG_H
#define DISKSPD_DEBUG_H

namespace diskspd {

	extern bool verbose;
	extern bool debug;

#define v_printf(format,args...)		\
	if (verbose) {	  \
		printf(format, ##args );		\
	}

#define d_printf(format,args...)		\
	if (debug) {	\
		fprintf(stderr, format, ##args );		 \
	}

} //namespace diskspd

#endif // DISKSPD_DEBUG_H
