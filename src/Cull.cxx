// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Cull.hxx"
#include "lib/fmt/ExceptionFormatter.hxx"
#include "event/co/Sleep.hxx" // TODO
#include "io/DirectoryReader.hxx"
#include "io/Open.hxx"
#include "io/uring/CoOperation.hxx"
#include "co/InvokeTask.hxx"
#include "co/Task.hxx"
#include "util/DeleteDisposer.hxx"

#include <cassert>

#include <errno.h>
#include <fcntl.h> // for O_DIRECTORY
#include <string.h> // for strerror()
#include <time.h> // for time()

#include <fmt/core.h> // TODO

static constexpr std::size_t MAX_FILES = 1024 * 1024;
static constexpr std::size_t MAX_STAT = 1024 * 1024;
static constexpr FileTime DISCARD_OLDER_THAN = std::chrono::hours{120 * 24};

class Cull::DirectoryRef {
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

struct Cull::File final : IntrusiveTreeSetHook<> {
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

constexpr FileTime
Cull::GetTime::operator()(const File &file) const noexcept
{
	return file.time;
}

static DevCachefiles::CullResult
CullFile(DevCachefiles &dev_cachefiles,
	 FileDescriptor directory_fd, std::string_view filename)
{
	if (fchdir(directory_fd.Get()) < 0)
		return DevCachefiles::CullResult::ERROR;

	// TODO use io_uring for this write() operation
	return dev_cachefiles.CullFile(filename);
}

class Cull::StatItem : public IntrusiveListHook<> {
	Cull &cull;

	DirectoryRef directory;

	std::string name;

	Co::InvokeTask invoke_task;

public:
	[[nodiscard]]
	StatItem(Cull &_cull, Directory &_directory, const char *_name) noexcept
		:cull(_cull), directory(_directory), name(_name) {}

	void Start(Uring::Queue &uring_) noexcept {
		invoke_task = Run(uring_);
		invoke_task.Start(BIND_THIS_METHOD(OnCompletion));
	}

private:
	[[nodiscard]]
	Co::InvokeTask Run(Uring::Queue &uring);

	void OnCompletion(std::exception_ptr error) noexcept {
		if (error)
			fmt::print(stderr, "Stat error: {}\n", error); // TODO handle properly

		cull.OnStatCompletion(*this);
	}
};

inline Co::InvokeTask
Cull::StatItem::Run(Uring::Queue &uring_)
{
	const auto stx = co_await Uring::CoStatx(uring_, directory->fd, name.c_str(),
						 AT_NO_AUTOMOUNT|AT_SYMLINK_NOFOLLOW|AT_STATX_DONT_SYNC,
						 STATX_TYPE|STATX_ATIME|STATX_BLOCKS);
	if (S_ISDIR(stx.stx_mode)) {
		/* before we scan another directory, make sure our
		   "stat" list isn't over-full (to put a cap on our
		   memory usage) */
		// TODO use some notification instead of polling periodically
		while (cull.stat.size() > MAX_STAT)
			co_await Co::Sleep(cull.event_loop, std::chrono::milliseconds{10});

		cull.AddDirectory(*directory, std::move(name));
	} else if (S_ISREG(stx.stx_mode)) {
		cull.AddFile(*directory, std::move(name), FileTime{stx.stx_atime.tv_sec},
			     stx.stx_blocks * 512ULL);
	}
}

Cull::Cull(EventLoop &_event_loop, Uring::Queue &_uring,
	   FileDescriptor _dev_cachefiles,
	   uint_least64_t _cull_files, std::size_t _cull_bytes,
	   Callback _callback)
	:event_loop(_event_loop), uring(_uring),
	 dev_cachefiles(_dev_cachefiles),
	 callback(_callback),
	 cull_files(_cull_files), cull_bytes(_cull_bytes),
	 discard_older_than(FileTime{time(nullptr)} - DISCARD_OLDER_THAN)
{
	assert(callback);
}

Cull::~Cull() noexcept
{
	files.clear_and_dispose(DeleteDisposer{});
	stat.clear_and_dispose(DeleteDisposer{});
}

void
Cull::Start(FileDescriptor root_fd)
{
	DirectoryRef root{DirectoryRef::Adopt{}, *new Directory(Directory::RootTag{}, OpenPath(root_fd, ".", O_DIRECTORY))};
	ScanDirectory(*root);
}

inline void
Cull::AddFile(Directory &parent, std::string &&name,
	      FileTime atime, uint_least64_t size)
{
	if (atime < discard_older_than) {
		CullFile(dev_cachefiles, parent.fd, name);
		return;
	}

	while (files.size() >= MAX_FILES || (files.size() >= cull_files && files_total_size > cull_bytes)) {
		auto &f = files.front();
		files_total_size -= f.size;
		files.pop_front();
		delete &f;
	}

	auto *file = new File(parent, std::move(name), atime, size);
	files.insert(*file);

	files_total_size += file->size;
}

[[gnu::pure]]
static bool
IsSpecialFilename(const char *s) noexcept
{
	return s[0] == '.' && (s[1] == 0 || (s[1] == '.' && s[2] == 0));
}

inline void
Cull::ScanDirectory(Directory &directory)
{
	DirectoryReader r{OpenDirectory(directory.fd, ".")};
	while (const char *name = r.Read()) {
		if (IsSpecialFilename(name))
			continue;

		auto *item = new StatItem(*this, directory, name);
		stat.push_back(*item);

		item->Start(uring);
	}
}

inline void
Cull::AddDirectory(Directory &parent, std::string &&name)
try {
	DirectoryRef directory{DirectoryRef::Adopt{}, *new Directory(parent, OpenPath(parent.fd, name.c_str(), O_DIRECTORY))};
	ScanDirectory(*directory);
} catch (...) {
	fmt::print(stderr, "Failed to scan directory: {}\n", std::current_exception());
}

inline void
Cull::DeleteFiles() noexcept
{
	fmt::print(stderr, "Cull: delete {} files, {} bytes\n", files.size(), files_total_size);

	std::size_t n_deleted_files = 0, n_busy = 0;
	uint_least64_t n_deleted_bytes = 0, n_errors = 0;

	files.clear_and_dispose([&](File *file){
#ifndef NDEBUG
		files_total_size -= file->size;
#endif

		switch (CullFile(dev_cachefiles, file->parent->fd, file->name.c_str())) {
		case DevCachefiles::CullResult::SUCCESS:
			++n_deleted_files;
			n_deleted_bytes += file->size;
			break;

		case DevCachefiles::CullResult::BUSY:
			++n_busy;
			break;

		case DevCachefiles::CullResult::ERROR:
			++n_errors;
		}

		delete file;
	});

	fmt::print(stderr, "Cull: deleted {} files, {} bytes; {} in use; {} errors\n", n_deleted_files, n_deleted_bytes, n_busy, n_errors);

	assert(files_total_size == 0);
}

inline void
Cull::OnStatCompletion(StatItem &item) noexcept
{
	stat.erase_and_dispose(stat.iterator_to(item), DeleteDisposer{});

	if (stat.empty()) {
		DeleteFiles();
		callback();
	}
}
