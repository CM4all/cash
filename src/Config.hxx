// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>
#include <forward_list>
#include <string>

struct Config {
	std::string dir;

	std::forward_list<std::string> kernel_config;

	uint_least8_t brun = 10, frun = 10;

	bool culling_disabled = false;
};

Config
LoadConfigFile(const char *path);
