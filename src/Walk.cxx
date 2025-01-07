// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Walk.hxx"
#include "WHandler.hxx"
#include "lib/fmt/ExceptionFormatter.hxx"
#include "io/DirectoryReader.hxx"
#include "io/Open.hxx"
#include "io/uring/CoOperation.hxx"
#include "co/InvokeTask.hxx"
#include "co/Task.hxx"
#include "util/DeleteDisposer.hxx"

#include <fcntl.h> // for O_DIRECTORY
#include <time.h> // for time()

#include <fmt/core.h> // TODO

static constexpr std::size_t MAX_FILES = 1024 * 1024;

/**
 * Limit on the number of concurrent statx() system calls.  Scanning
 * new directories is suspended until we're below #RESUME_STAT.
 */
static constexpr std::size_t MAX_STAT = 16 * 1024;

/**
 * Resume submitting new statx() system calls when the number of
 * pending calls goes below this number.
 */
static constexpr std::size_t RESUME_STAT = 4 * 1024;

static constexpr FileTime DISCARD_OLDER_THAN = std::chrono::hours{120 * 24};

class Walk::StatItem : public IntrusiveListHook<> {
	Walk &walk;

	WalkDirectoryRef directory;

	std::string name;

	Co::InvokeTask invoke_task;

public:
	[[nodiscard]]
	StatItem(Walk &_walk, WalkDirectory &_directory, const char *_name) noexcept
		:walk(_walk), directory(_directory), name(_name) {}

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

		walk.OnStatCompletion(*this);
	}
};

inline Co::InvokeTask
Walk::StatItem::Run(Uring::Queue &uring_)
{
	const auto stx = co_await Uring::CoStatx(uring_, directory->fd, name.c_str(),
						 AT_NO_AUTOMOUNT|AT_SYMLINK_NOFOLLOW|AT_STATX_DONT_SYNC,
						 STATX_TYPE|STATX_ATIME|STATX_BLOCKS);
	if (S_ISDIR(stx.stx_mode)) {
		/* before we scan another directory, make sure our
		   "stat" list isn't over-full (to put a cap on our
		   memory usage) */
		while (walk.stat.size() > MAX_STAT)
			co_await walk.resume_stat;

		co_await walk.AddDirectory(*directory, std::move(name));
	} else if (S_ISREG(stx.stx_mode)) {
		walk.AddFile(*directory, std::move(name), FileTime{stx.stx_atime.tv_sec},
			     stx.stx_blocks * 512ULL);
	}
}

Walk::Walk(Uring::Queue &_uring,
	   uint_least64_t _collect_files, std::size_t _collect_bytes,
	   WalkHandler &_handler)
	:uring(_uring),
	 handler(_handler),
	 collect_files(_collect_files), collect_bytes(_collect_bytes),
	 discard_older_than(FileTime{time(nullptr)} - DISCARD_OLDER_THAN)
{
}

Walk::~Walk() noexcept
{
	stat.clear_and_dispose(DeleteDisposer{});
}

void
Walk::Start(FileDescriptor root_fd)
{
	WalkDirectoryRef root{WalkDirectoryRef::Adopt{}, *new WalkDirectory(uring, WalkDirectory::RootTag{}, OpenPath(root_fd, ".", O_DIRECTORY))};
	ScanDirectory(*root, OpenDirectory(root_fd, "."));
}

inline void
Walk::AddFile(WalkDirectory &parent, std::string &&name,
	      FileTime atime, uint_least64_t size)
{
	if (atime < discard_older_than) {
		handler.OnWalkAncient(parent, std::move(name), size);
		return;
	}

	while (result.files.size() >= MAX_FILES ||
	       (result.files.size() >= collect_files && result.total_bytes > collect_bytes)) {
		auto &f = result.files.front();
		result.total_bytes -= f.size;
		result.files.pop_front();
		delete &f;
	}

	auto *file = new File(parent, std::move(name), atime, size);
	result.files.insert(*file);

	result.total_bytes += file->size;
}

[[gnu::pure]]
static bool
IsSpecialFilename(const char *s) noexcept
{
	return s[0] == '.' && (s[1] == 0 || (s[1] == '.' && s[2] == 0));
}

inline void
Walk::ScanDirectory(WalkDirectory &directory, UniqueFileDescriptor &&fd)
{
	// TODO eliminate this method, use only CoScanDirectory

	DirectoryReader r{std::move(fd)};
	while (const char *name = r.Read()) {
		if (IsSpecialFilename(name))
			continue;

		auto *item = new StatItem(*this, directory, name);
		stat.push_back(*item);

		item->Start(uring);
	}
}

inline Co::Task<void>
Walk::CoScanDirectory(WalkDirectory &directory, UniqueFileDescriptor &&fd)
{
	DirectoryReader r{std::move(fd)};
	while (const char *name = r.Read()) {
		if (IsSpecialFilename(name))
			continue;

		/* throttle if there are too many concurrent statx
                   system calls */
		while (stat.size() > MAX_STAT) [[unlikely]]
			co_await resume_stat;

		auto *item = new StatItem(*this, directory, name);
		stat.push_back(*item);

		item->Start(uring);
	}
}

inline Co::Task<void>
Walk::AddDirectory(WalkDirectory &parent, std::string &&name)
try {
	WalkDirectoryRef directory{
		WalkDirectoryRef::Adopt{},
		*new WalkDirectory(uring, parent, co_await Uring::CoOpen(uring, parent.fd, name.c_str(), O_PATH|O_DIRECTORY, 0)),
	};

	co_await CoScanDirectory(*directory, co_await Uring::CoOpen(uring, directory->fd, ".", O_DIRECTORY, 0));
} catch (...) {
	fmt::print(stderr, "Failed to scan directory: {}\n", std::current_exception());
}

inline void
Walk::OnStatCompletion(StatItem &item) noexcept
{
	const bool was_too_many_stat = stat.size() >= RESUME_STAT;

	stat.erase_and_dispose(stat.iterator_to(item), DeleteDisposer{});

	if (was_too_many_stat && stat.size() < RESUME_STAT)
		resume_stat.ResumeAll();

	if (stat.empty())
		handler.OnWalkFinished(std::move(result));
}
