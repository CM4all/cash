// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/uring/Close.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/IntrusiveTreeSet.hxx"

#include <cassert>
#include <chrono>
#include <utility> // for std::exchange()

#include <time.h> // for time_t

using FileTime = std::chrono::duration<time_t>;

/**
 * Represents a directory inside "/var/cache/fscache/cache".  It is
 * kept around because it manages an O_PATH file descriptor for
 * efficient file access inside this directory.
 *
 * Instances of this object are reference-counted (except for the
 * #root instance).  Use #WalkDirectoryRef to use these reference
 * counts safely.
 */
struct WalkDirectory {
	Uring::Queue &uring;

	WalkDirectory *const parent;

	/**
	 * An O_PATH file descriptor.
	 */
	const FileDescriptor fd;

	unsigned ref = 1;

	struct RootTag {};
	WalkDirectory(Uring::Queue &_uring, RootTag,
		      UniqueFileDescriptor &&_fd) noexcept
		:uring(_uring), parent(nullptr), fd(_fd.Release()) {}

	WalkDirectory(Uring::Queue &_uring, WalkDirectory &_parent,
		      UniqueFileDescriptor &&_fd)
		:uring(_uring),
		 parent(&_parent.Ref()), fd(_fd.Release()) {}

	~WalkDirectory() noexcept {
		Uring::Close(&uring, fd);

		if (parent != nullptr)
			parent->Unref();
	}

	WalkDirectory(const WalkDirectory &) = delete;
	WalkDirectory &operator=(const WalkDirectory &) = delete;

	WalkDirectory &Ref() noexcept {
		++ref;
		return *this;
	}

	void Unref() noexcept {
		if (--ref == 0)
			delete this;
	}
};

class WalkDirectoryRef {
	WalkDirectory *directory = nullptr;

public:
	[[nodiscard]]
	explicit WalkDirectoryRef(WalkDirectory &_directory) noexcept
		:directory(&_directory.Ref()) {}

	struct Adopt {};

	[[nodiscard]]
	WalkDirectoryRef(Adopt, WalkDirectory &_directory) noexcept
		:directory(&_directory) {}

	WalkDirectoryRef(WalkDirectoryRef &&src) noexcept
		:directory(std::exchange(src.directory, nullptr)) {}

	~WalkDirectoryRef() noexcept {
		if (directory != nullptr)
			directory->Unref();
	}

	WalkDirectoryRef &operator=(const WalkDirectoryRef &) = delete;

	[[nodiscard]]
	WalkDirectory &operator*() const noexcept{
		assert(directory != nullptr);

		return *directory;
	}

	[[nodiscard]]
	WalkDirectory *operator->() const noexcept{
		assert(directory != nullptr);

		return directory;
	}
};

/**
 * The result struct for #Walk, passed to #WalkHandler.
 */
struct WalkResult {
	struct File final : IntrusiveTreeSetHook<> {
		WalkDirectoryRef parent;

		const FileTime time;

		const uint_least64_t size;

		std::string name;

		[[nodiscard]]
		File(WalkDirectory &_parent, std::string &&_name,
		     FileTime _time, const uint_least64_t _size) noexcept
			:parent(_parent), time(_time), size(_size),
			name(std::move(_name)) {}

		File(const File &) = delete;
		File &operator=(const File &) = delete;
	};

	struct GetTime {
		constexpr FileTime operator()(const File &file) const noexcept {
			return file.time;
		}
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
	uint_least64_t total_bytes = 0;

	~WalkResult() noexcept {
		files.clear_and_dispose(DeleteDisposer{});
	}
};
