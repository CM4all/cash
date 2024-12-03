// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "DevCachefiles.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/BindMethod.hxx"
#include "util/IntrusiveList.hxx"
#include "util/IntrusiveTreeSet.hxx"

#include <chrono>
#include <cstdint>
#include <string>

#include <time.h> // for time_t

class EventLoop;
namespace Uring { class Queue; }

using FileTime = std::chrono::duration<time_t>;

/**
 * This class represents the cachefiles "cull" operation.  It walks
 * the whole tree and deletes the files that havn't been accessed for
 * the longest time.  Upon completion, the given callback is invoked.
 */
class Cull {
	/**
	 * Represents a directory inside "/var/cache/fscache/cache".
	 * It is kept around because it manages an O_PATH file
	 * descriptor for efficient file access inside this directory.
	 *
	 * Instances of this object are reference-counted (except for
	 * the #root instance).  Use #DirectoryRef to use these
	 * reference counts safely.
	 */
	struct Directory {
		Directory *const parent;

		/**
		 * An O_PATH file descriptor.
		 */
		const UniqueFileDescriptor fd;

		unsigned ref = 1;

		struct RootTag {};
		Directory(RootTag, UniqueFileDescriptor &&_fd) noexcept
			:parent(nullptr), fd(std::move(_fd)) {}

		Directory(Directory &_parent, UniqueFileDescriptor &&_fd)
			:parent(&_parent.Ref()), fd(std::move(_fd)) {}

		~Directory() noexcept {
			if (parent != nullptr)
				parent->Unref();
		}

		Directory(const Directory &) = delete;
		Directory &operator=(const Directory &) = delete;

		Directory &Ref() noexcept {
			++ref;
			return *this;
		}

		void Unref() noexcept {
			if (--ref == 0)
				delete this;
		}
	};

	class DirectoryRef;
	struct File;

	struct GetTime {
		constexpr FileTime operator()(const File &file) const noexcept;
	};

	/**
	 * This uses reverse comparison so we have the newest items
	 * first.  When the tree is full, the first item is removed,
	 * leaving older items in the list.
	 */
	struct ReverseCompareTime {
		constexpr std::weak_ordering operator()(FileTime a, FileTime b) const noexcept {
			return b <=> a;
		}
	};

	using TimeSortedFiles = IntrusiveTreeSet<File,
		IntrusiveTreeSetOperators<File, GetTime, ReverseCompareTime>,
		IntrusiveTreeSetBaseHookTraits<File>,
		IntrusiveTreeSetOptions{.constant_time_size=true}>;

	EventLoop &event_loop;
	Uring::Queue &uring;

	DevCachefiles dev_cachefiles;

	using Callback = BoundMethod<void() noexcept>;
	const Callback callback;

	class StatItem;
	IntrusiveList<StatItem, IntrusiveListBaseHookTraits<StatItem>, IntrusiveListOptions{.constant_time_size=true}> stat;

	/**
	 * A tree of #File objects sorted by time of last access,
	 * newest first.  This is where we collect files that were
	 * just scanned.  At the end of the scan, all files that
	 * remain in this list will be deleted.
	 */
	TimeSortedFiles files;

	/**
	 * The total size of all #files [bytes].
	 */
	uint_least64_t files_total_size = 0;

	/**
	 * The number of files that shall be culled to go below
	 * "fcull".
	 */
	const std::size_t cull_files;

	/**
	 * The number of bytes that shall be culled to go below
	 * "bcull".
	 */
	const uint_least64_t cull_bytes;

	/**
	 * Cull all files which havn't been accessed before this time
	 * stamp.
	 */
	const FileTime discard_older_than;

public:
	[[nodiscard]]
	Cull(EventLoop &_event_loop, Uring::Queue &_uring,
	     FileDescriptor _dev_cachefiles,
	     std::size_t _cull_files, uint_least64_t _cull_bytes,
	     Callback _callback);
	~Cull() noexcept;

	void Start(FileDescriptor root_fd);

private:
	void AddDirectory(Directory &parent, std::string &&name);
	void AddFile(Directory &parent, std::string &&name,
		     FileTime atime, uint_least64_t size);

	void ScanDirectory(Directory &directory);

	void DeleteFiles() noexcept;

	void OnStatCompletion(StatItem &item) noexcept;
};
