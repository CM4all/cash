// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct Options {
	const char *configfile = "/etc/cachefilesd.conf";
};

Options
ParseCommandLine(int argc, char **argv) noexcept;
