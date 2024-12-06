// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct Options {
	const char *configfile = "/etc/cachefilesd.conf";
};

Options
ParseCommandLine(int argc, char **argv) noexcept;
