// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "io/UniqueFileDescriptor.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/IntrusiveTreeSet.hxx"

#include <chrono>

#include <time.h> // for time_t

using FileTime = std::chrono::duration<time_t>;

/**
 * The result struct for #Walk, passed to #WalkHandler.
 */
struct WalkResult {
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

	class DirectoryRef {
		Directory &directory;

	public:
		[[nodiscard]]
		explicit DirectoryRef(Directory &_directory) noexcept
			:directory(_directory.Ref()) {}

		struct Adopt {};

		[[nodiscard]]
		DirectoryRef(Adopt, Directory &_directory) noexcept
			:directory(_directory) {}

		~DirectoryRef() noexcept {
			directory.Unref();
		}

		DirectoryRef(const DirectoryRef &) = delete;
		DirectoryRef &operator=(const DirectoryRef &) = delete;

		[[nodiscard]]
		Directory &operator*() const noexcept{
			return directory;
		}

		[[nodiscard]]
		Directory *operator->() const noexcept{
			return &directory;
		}
	};

	struct File final : IntrusiveTreeSetHook<> {
		DirectoryRef parent;

		const FileTime time;

		const uint_least64_t size;

		const std::string name;

		[[nodiscard]]
		File(Directory &_parent, std::string &&_name,
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
