// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/FileDescriptor.hxx"
#include "util/StringBuffer.hxx"

#include <cstddef>
#include <span>
#include <string_view>

#include <limits.h> // for NAME_MAX

/**
 * OO wrapper for a /dev/cachefiles file descriptor (non-owning).
 */
class DevCachefiles {
	FileDescriptor fd;

public:
	[[nodiscard]]
	explicit DevCachefiles(FileDescriptor _fd) noexcept
		:fd(_fd) {}

	DevCachefiles(const DevCachefiles &) = delete;
	DevCachefiles &operator=(const DevCachefiles &) = delete;

	FileDescriptor GetFileDescriptor() const noexcept {
		return fd;
	}

	using Buffer = StringBuffer<NAME_MAX + 8>;

	static std::span<const std::byte> FormatCullFile(Buffer &buffer, std::string_view filename) noexcept;

	enum class CullResult {
		SUCCESS,
		BUSY,
		ERROR,
	};

	[[nodiscard]]
	static CullResult CheckCullFileResult(std::string_view filename, int res) noexcept;
};
