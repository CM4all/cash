// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "DevCachefiles.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "util/SpanCast.hxx"

#include <fmt/core.h>

#include <limits.h> // for NAME_MAX

DevCachefiles::CullResult
DevCachefiles::CullFile(std::string_view filename) noexcept
{
	if (filename.size() >= NAME_MAX)
		return CullResult::ERROR;

	// unfortunately, fscache is buggy with writev()
	const auto buffer = FmtBuffer<NAME_MAX + 8>("cull {}", filename);

	if (fd.Write(AsBytes(std::string_view{buffer})) < 0) {
		switch (const int e = errno) {
		case ESTALE:
		case ENOENT:
			return CullResult::SUCCESS;

		case EBUSY:
			return CullResult::BUSY;

		default:
			fmt::print(stderr, "Failed to cull {:?}: {}\n",
				   filename, strerror(e));
			return CullResult::ERROR;
		}
	}

	return CullResult::SUCCESS;
}
