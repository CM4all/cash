// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "DevCachefiles.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/IterableSplitString.hxx"
#include "util/SpanCast.hxx"
#include "util/StringSplit.hxx"

#include <fmt/core.h>

#include <string.h> // for strerror()

using std::string_view_literals::operator""sv;

DevCachefiles::DevCachefiles(EventLoop &event_loop, UniqueFileDescriptor _fd,
			     BoundMethod<void() noexcept> _cull_callback) noexcept
	:device(event_loop, BIND_THIS_METHOD(OnDeviceReady), _fd.Release()),
	 cull_callback(_cull_callback)
{
}

DevCachefiles::~DevCachefiles() noexcept
{
	device.Close();
}

void
DevCachefiles::OnDeviceReady([[maybe_unused]] unsigned events) noexcept
{
	char buffer[1024];
	ssize_t nbytes = GetFileDescriptor().Read(std::as_writable_bytes(std::span{buffer}));
	if (nbytes <= 0) {
		// TODO
		Disable();
		return;
	}

	const std::string_view line{buffer, static_cast<std::size_t>(nbytes)};

	bool start_cull = false;
	for (const std::string_view i : IterableSplitString(line, ' ')) {
		const auto [name, value] = Split(i, '=');
		if (name == "cull"sv)
			start_cull = value != "0"sv;
	}

	if (start_cull)
		cull_callback();
}

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
