// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "DevCachefiles.hxx"
#include "util/SpanCast.hxx"

#include <fmt/core.h>

#include <string.h> // for strerror()

std::span<const std::byte>
DevCachefiles::FormatCullFile(Buffer &buffer, std::string_view filename) noexcept
{
	if (filename.size() >= NAME_MAX)
		return {};

	auto i = fmt::format_to(buffer.data(), "cull {}", filename);
	return AsBytes(std::string_view{buffer.data(), i});
}

DevCachefiles::CullResult
DevCachefiles::CheckCullFileResult(std::string_view filename, int res) noexcept
{
	if (res < 0) {
		switch (res) {
		case -ESTALE:
		case -ENOENT:
			return CullResult::SUCCESS;

		case -EBUSY:
			return CullResult::BUSY;

		default:
			fmt::print(stderr, "Failed to cull {:?}: {}\n",
				   filename, strerror(-res));
			return CullResult::ERROR;
		}
	}

	return CullResult::SUCCESS;
}
