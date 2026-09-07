// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "WResult.hxx"
#include "co/MultiResume.hxx"
#include "util/IntrusiveList.hxx"

#include <chrono>
#include <cstdint>
#include <string>

class FileDescriptor;
class UniqueFileDescriptor;
class EventLoop;
namespace Uring { class Queue; }
namespace Co { template <typename T> class Task; }
class WalkHandler;

/**
 * Walk a filesystem tree and collect files that have not been accessed
 * for the longest time.  Pass a #WalkHandler to the constructor and
 * call Start() to start the walk operation.  The walk will happen
 * asynchronously in the #EventLoop (using io_uring).
 */
class Walk final {
	EventLoop &event_loop;
	Uring::Queue &uring;

	WalkHandler &handler;

	class StatItem;
	IntrusiveList<StatItem, IntrusiveListBaseHookTraits<StatItem>, IntrusiveListOptions{.constant_time_size=true}> stat;

	/**
	 * This is awaited on by coroutines which want to add items to
	 * #stat when there are too many pending operations already.
	 * It will be resumed when enough operations have completed.
	 */
	Co::MultiResume resume_stat;

	/**
	 * Throttles readdir() calls.
	 */
	Co::MultiResume resume_scan;

	std::chrono::steady_clock::time_point next_yield{};

	WalkResult result;

	using File = WalkResult::File;

	/**
	 * Collect this number of files.  May collect more than that
	 * if #collect_bytes has not yet been reached.
	 */
	const std::size_t collect_files;

	/**
	 * Collect this number of bytes.  May collect more than that
	 * if #collect_files has not yet been reached.
	 */
	const uint_least64_t collect_bytes;

	/**
	 * Cull all files which havn't been accessed before this time
	 * stamp.
	 */
	const FileTime discard_older_than;

	bool suspend_scan = false;

public:
	[[nodiscard]]
	Walk(EventLoop &_event_loop, Uring::Queue &_uring,
	     std::size_t _collect_files, uint_least64_t _collect_bytes,
	     WalkHandler &_handler);
	~Walk() noexcept;

	void Start(FileDescriptor root_fd);

private:
	Co::Task<void> AddDirectory(WalkDirectory &parent, std::string &&name);
	void AddFile(WalkDirectory &parent, std::string &&name,
		     FileTime atime, uint_least64_t size);

	void ScanDirectory(WalkDirectory &directory, UniqueFileDescriptor &&fd);
	Co::Task<void> CoScanDirectory(WalkDirectory &directory, UniqueFileDescriptor &&fd);

	void OnStatCompletion(StatItem &item) noexcept;
};
