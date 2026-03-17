// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/uring/Close.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/StaticVector.hxx"

#include <algorithm> // for std::push_heap(), std::pop_heap()
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

	WalkDirectoryRef &operator=(WalkDirectoryRef &&src) noexcept {
		using std::swap;
		swap(directory, src.directory);
		return *this;
	}

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
	struct File final {
		WalkDirectoryRef parent;

		FileTime time;

		uint_least64_t size;

		std::string name;

		[[nodiscard]]
		File(WalkDirectory &_parent, std::string &&_name,
		     FileTime _time, const uint_least64_t _size) noexcept
			:parent(_parent), time(_time), size(_size),
			name(std::move(_name)) {}

		File(File &&) noexcept = default;
		File &operator=(File &&) noexcept = default;

		constexpr bool operator<(const File &other) const noexcept {
			return time < other.time;
		}
	};

	static constexpr std::size_t MAX_FILES = 1024 * 1024;

	/**
	 * A max-heap #File objects by time of last access, newest at
	 * the top.  This is where we collect files that were just
	 * scanned.  At the end of the scan, all files that remain in
	 * this list will be deleted.
	 */
	StaticVector<File, MAX_FILES> files;

	/**
	 * The total size of all #files [bytes].
	 */
	uint_least64_t total_bytes = 0;

	/**
	 * Pop the most recently accessed file from the heap.
	 */
	void Pop() noexcept {
		total_bytes -= files.front().size;
		std::pop_heap(files.begin(), files.end());
		files.pop_back();
	}

	/**
	 * Prepare for pushing a new file on the heap.  Evicts the
	 * most recently accessed file if the heap is already full.
	 * Returns false if the specified time is more recent than the
	 * top of this heap (and thus the file is not a candidate and
	 * must not be pushed).
	 */
	[[nodiscard]]
	bool PreparePush(FileTime new_time) noexcept {
		if (files.full()) {
			if (new_time >= files.front().time)
				return false;

			Pop();
		}

		return true;
	}

	/**
	 * Construct a file in-place on the heap.
	 */
	void Emplace(WalkDirectory &parent, std::string &&name,
		     FileTime time, const uint_least64_t size) noexcept {
		assert(!files.full());

		total_bytes += size;
		files.emplace_back(parent, std::move(name), time, size);
		std::push_heap(files.begin(), files.end());
	}
};
