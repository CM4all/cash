// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/PipeEvent.hxx"
#include "util/StringBuffer.hxx"

#include <cstddef>
#include <exception>
#include <span>
#include <string_view>

#include <limits.h> // for NAME_MAX

class UniqueFileDescriptor;

class DevCachefilesHandler {
public:
	virtual void OnDevCachefilesStartCull() noexcept = 0;
	virtual void OnDevCachefilesError(std::exception_ptr &&error) noexcept = 0;
};

/**
 * OO wrapper for a /dev/cachefiles file descriptor (non-owning).
 */
class DevCachefiles {
	PipeEvent device;

	DevCachefilesHandler &handler;

public:
	[[nodiscard]]
	DevCachefiles(EventLoop &event_loop, UniqueFileDescriptor _fd,
		      DevCachefilesHandler &_handler) noexcept;

	~DevCachefiles() noexcept;

	DevCachefiles(const DevCachefiles &) = delete;
	DevCachefiles &operator=(const DevCachefiles &) = delete;

	FileDescriptor GetFileDescriptor() const noexcept {
		return device.GetFileDescriptor();
	}

	void Enable() noexcept {
		device.ScheduleWrite();
	}

	void Disable() noexcept {
		device.Cancel();
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

private:
	void OnDeviceReady(unsigned events) noexcept;
};
