// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Options.hxx"
#include "version.h"

#include <fmt/core.h>

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h> // for EXIT_SUCCESS
#include <string.h>
#include <unistd.h> // for exit()

static void
PrintVersion() noexcept
{
	fmt::print("cachefilesd version " VERSION "\n");
}

static void
PrintHelp(const char *argv0) noexcept
{
	if (argv0 == nullptr)
		argv0 = "cachefilesd";

	fmt::print(stderr,
		   "Format:\n"
		   "  {} [-f <configfile>]\n"
		   "  {} -v\n"
		   "\n"
		   "Options:\n"
		   "  -f <configfile>\n"
		   "  -v\tPrint version and exit\n"
		   "\tRead the specified configuration file instead of"
		   " /etc/cachefiles.conf\n",
		   argv0, argv0);
}

Options
ParseCommandLine(int argc, char **argv) noexcept
{
	/* handle help request */
	if (argc == 2 && strcmp(argv[1], "--help") == 0) {
		PrintHelp(argv[0]);
		exit(EXIT_FAILURE);
	}

	if (argc == 2 && strcmp(argv[1], "--version") == 0) {
		PrintVersion();
		exit(EXIT_SUCCESS);
	}

	Options options;

	int opt;
	while ((opt = getopt(argc, argv, "dsnNf:p:v")) != EOF) {
		switch (opt) {
		case 'd':
			/* turn on debugging */
			// not implemented
			break;

		case 's':
			/* disable syslog writing */
			// not implemented
			break;

		case 'n':
			/* don't daemonise */
			// not implemented
			break;

		case 'N':
			/* disable culling */
			// not implemented
			break;

		case 'f':
			/* use a specific config file */
			options.configfile = optarg;
			break;

		case 'p':
			/* use a specific PID file */
			// not implemented
			break;

		case 'v':
			/* print the version and exit */
			PrintVersion();
			exit(EXIT_SUCCESS);

		default:
			fmt::print(stderr, "Unknown commandline option '{}'", optopt);
			exit(EXIT_FAILURE);
		}
	}

	return options;
}
