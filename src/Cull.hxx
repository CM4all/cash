// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "DevCachefiles.hxx"
#include "WHandler.hxx"
#include "Chdir.hxx"
#include "event/DeferEvent.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/BindMethod.hxx"
#include "util/IntrusiveList.hxx"

#include <cstdint>
#include <memory>
#include <string>

#include <time.h> // for time_t

namespace Uring { class Queue; }
class Walk;

/**
 * This class represents the cachefiles "cull" operation.  It walks
 * the whole tree and deletes the files that havn't been accessed for
 * the longest time.  Upon completion, the given callback is invoked.
 */
class Cull final : WalkHandler {
	Uring::Queue &uring;
	DevCachefiles dev_cachefiles;

	using Callback = BoundMethod<void() noexcept>;
	const Callback callback;

	std::unique_ptr<Walk> walk;

	Chdir chdir;

	class CullFileOperation;
	IntrusiveList<CullFileOperation> operations, new_operations;

	/**
	 * Start #new_operations and move them to #operations.
	 */
	DeferEvent defer_start;

	std::size_t n_deleted_files = 0, n_busy = 0;
	uint_least64_t n_deleted_bytes = 0, n_errors = 0;

public:
	[[nodiscard]]
	Cull(EventLoop &event_loop, Uring::Queue &_uring,
	     FileDescriptor _dev_cachefiles,
	     std::size_t _cull_files, uint_least64_t _cull_bytes,
	     Callback _callback);
	~Cull() noexcept;

	void Start(FileDescriptor root_fd);

private:
	void OnDeferredStart() noexcept;
	void OperationFinished(CullFileOperation &op) noexcept;
	void Finish() noexcept;

	// virtual methods from WalkHandler
	void OnWalkAncient(WalkDirectory &directory,
			   std::string &&filename,
			   uint_least64_t size) noexcept override;
	void OnWalkFinished(WalkResult &&result) noexcept override;
};
