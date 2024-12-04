// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "DevCachefiles.hxx"
#include "Walk.hxx"
#include "WHandler.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/BindMethod.hxx"

#include <cstdint>
#include <string>

#include <time.h> // for time_t

/**
 * This class represents the cachefiles "cull" operation.  It walks
 * the whole tree and deletes the files that havn't been accessed for
 * the longest time.  Upon completion, the given callback is invoked.
 */
class Cull final : WalkHandler {
	DevCachefiles dev_cachefiles;

	using Callback = BoundMethod<void() noexcept>;
	const Callback callback;

	Walk walk;

public:
	[[nodiscard]]
	Cull(Uring::Queue &_uring,
	     FileDescriptor _dev_cachefiles,
	     std::size_t _cull_files, uint_least64_t _cull_bytes,
	     Callback _callback);
	~Cull() noexcept;

	void Start(FileDescriptor root_fd);

private:
	// virtual methods from WalkHandler
	void OnWalkAncient(FileDescriptor directory_fd,
			   std::string_view filename) noexcept override;
	void OnWalkFinished(WalkResult &&result) noexcept override;
};
