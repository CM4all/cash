// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/FileDescriptor.hxx"

#include <string_view>

/**
 * OO wrapper for a /dev/cachefiles file descriptor (non-owning).
 */
class DevCachefiles {
	FileDescriptor fd;

public:
	explicit DevCachefiles(FileDescriptor _fd) noexcept
		:fd(_fd) {}

	DevCachefiles(const DevCachefiles &) = delete;
	DevCachefiles &operator=(const DevCachefiles &) = delete;

	[[gnu::pure]]
	bool IsInUse(std::string_view filename) noexcept;

	enum class CullResult {
		SUCCESS,
		BUSY,
		ERROR,
	};

	CullResult CullFile(std::string_view filename) noexcept;
};
