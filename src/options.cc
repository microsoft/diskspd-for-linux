// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <limits>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include<argp.h>
#include<assert.h>

#include "options.h"

namespace diskspd
{

	/*
	 *	*****************************
	 *	utility functions for parsers
	 *	*****************************
	 */

	bool Options::is_numeric(const char *arg) {
		// if first digit is 0, that should be the whole string
		if (arg[0] == '0' && arg[1] != '\0') return false;
		// iterate through characters
		for(;*arg;arg=&arg[1]) {
			if (*arg < '0' || *arg > '9') {
				return false;
			}
		}
		return true;
	}

	bool Options::valid_byte_size(const char *arg) {
		assert(arg);

		static const char validchars[] = {'K','M','G','b','\0'};

		// fail if it doesn't start with a non-zero number
		if (arg[0] <= '0' || arg[0] > '9') {
			fprintf(stderr, "Invalid byte size!\n");
			return false;
		}

		for (char *c = (char *)arg; *c; c++) {
			if (*c < '0' || *c > '9') {
				bool valid = false;
				for (int i = 0; i < strlen(validchars); ++i) {
					// must be a valid char and at the end of the string
					if (*c == validchars[i] && c[1] == '\0') {
						valid = true;
						break;
					}
				}
				if (!valid) {
					fprintf(stderr, "Invalid byte size!\n");
					return false;
				}
			}
		}

		return true;
	}

	unsigned long long Options::getSizeMultiplier(char specifier, size_t block_size) {
		 switch (specifier) {
			case 'K':
				return 1024;
			case 'M':
				return 1048576;
			case 'G':
				return 1073741824;
			case 'b':
				return block_size;
			default:
				assert(!"Invalid size specifier!");
		 }
	}

	unsigned long long Options::byte_size_from_arg(const char * curr_arg, size_t block_size) {

		char * endptr = nullptr;
		// convert to a 64 bit integer
		unsigned long long offset = strtoull(curr_arg, &endptr, 10);
		assert(endptr);
		// if there's another character after the number, use it to get a different multiplier
		unsigned long long multiplier = 1;	  // bytes
		if (*endptr) {
			multiplier = getSizeMultiplier(*endptr, block_size);
		}
		// unsigned overflow check
		long long final_offset = offset * multiplier;
		assert (final_offset >= 0 && "Overflow error");

		return static_cast<unsigned long long>(final_offset);
	}

	/*
	 *	*******************
	 *	parse_args related
	 *	*******************
	 */

	/**
	 *	Just a wrapper for parse_arg, passed into the argp library
	 */
	static error_t arg_parser(int key, char *arg, struct argp_state *state) {
		// get Options object
		Options* options = (Options*)state->input;
		// try to parse the arg
		return options->parse_arg(key, arg);
	}

	/**
	 *	Argument parser function, mainly just maps the arguments to their type and sticks them in
	 *	the map that the user has access to
	 */
	error_t Options::parse_arg(int key, char *arg) {

		// if it's in the map of expected options
		if (opt_map.count(key)) {

			// get the option from the opt_map
			DiskspdOption& option = opt_map[key];

			// check for duplicate args
			if (opts.count(option.type)) {
				fprintf(stderr, "Option -%c already specified!\n", (char)key);
				return EINVAL;
			}
			// add to map of parsed options
			opts[option.type] = key;

			// get argument and validate it
			if (arg) {
				option.arg = std::string(arg);

				if ((option.flags & OPT_NUMERIC) && !is_numeric(arg)) {
					fprintf(stderr, "Argument to option -%c was invalid!\n", (char)key);
					return EINVAL;
				} else if ((option.flags & OPT_BYTE_SIZE) && !valid_byte_size(arg)) {
					fprintf(stderr, "Argument to option -%c was invalid!\n", (char)key);
					return EINVAL;
				} else if ((option.flags & OPT_NON_ZERO) &&
						(arg[0] == '0' && (arg[1] < '1' || arg[1] > '9'))) {
					fprintf(stderr, "Argument to option -%c was invalid!\n", (char)key);
					return EINVAL;
				} // else do nothing - it will have to be validated by profile.cc
			}

		} else if (key == ARGP_KEY_ARG) {
			// otherwise it's a non-option (a target for diskspd), so push it to the vector
			non_opts.push_back(std::string(arg));

		} else {
			return ARGP_ERR_UNKNOWN;
		}
		return 0;
	}

	/**
	 *	Do the actual argument parsing with argparse
	 */
	bool Options::parse_args(int argc, char ** argv) {

		static bool donecopy = false;
		// enough space for a zero on the end
		static std::vector<argp_option> argparse_options(opt_map.size()+1);
		// copy options to array
		if (!donecopy) {
			unsigned i = 0;
			for (auto it = opt_map.begin(); it != opt_map.end(); ++it, ++i) {
				memcpy(&argparse_options[i], &(it->second.opt), sizeof(argp_option));
			}
			// zero last element
			memset(&argparse_options[argparse_options.size()-1], 0, sizeof(argp_option));
		}
		donecopy = true;

		static const struct argp the_argp = {
			options:&argparse_options[0],
			parser:arg_parser,
			args_doc:non_opt_doc,
			doc:docstring,
			children:NULL,
			help_filter:NULL,
			argp_domain:NULL
		};

		if (argc <= 1) {
			fprintf(stderr, "No target files specified!\n");
			argp_help((const struct argp *)&the_argp, stderr, ARGP_HELP_SHORT_USAGE | ARGP_HELP_SEE, argv[0]);
			return false;
		}

		if(argp_parse((const struct argp *)&the_argp, argc, argv, 0, NULL, (void *)this)) {
			return false;
		}

		return true;
	}

} //namespace diskspd

