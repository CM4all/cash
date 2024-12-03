// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Cull.hxx"

#include <cassert>

#include <fmt/core.h> // TODO

static DevCachefiles::CullResult
CullFile(DevCachefiles &dev_cachefiles,
	 FileDescriptor directory_fd, std::string_view filename)
{
	if (fchdir(directory_fd.Get()) < 0)
		return DevCachefiles::CullResult::ERROR;

	// TODO use io_uring for this write() operation
	return dev_cachefiles.CullFile(filename);
}

Cull::Cull(EventLoop &_event_loop, Uring::Queue &_uring,
	   FileDescriptor _dev_cachefiles,
	   uint_least64_t _cull_files, std::size_t _cull_bytes,
	   Callback _callback)
	:dev_cachefiles(_dev_cachefiles),
	 callback(_callback),
	 walk(_event_loop, _uring, _cull_files, _cull_bytes, *this)
{
	assert(callback);
}

Cull::~Cull() noexcept = default;

void
Cull::Start(FileDescriptor root_fd)
{
	walk.Start(root_fd);
}

void
Cull::OnWalkAncient(FileDescriptor directory_fd,
		    std::string_view filename) noexcept
{
	CullFile(dev_cachefiles, directory_fd, filename);
}

void
Cull::OnWalkFinished(WalkResult &&result) noexcept
{
	fmt::print(stderr, "Cull: delete {} files, {} bytes\n",
		   result.files.size(), result.total_bytes);

	std::size_t n_deleted_files = 0, n_busy = 0;
	uint_least64_t n_deleted_bytes = 0, n_errors = 0;

	result.files.clear_and_dispose([&](WalkResult::File *file){
#ifndef NDEBUG
		result.total_bytes -= file->size;
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

	assert(result.total_bytes == 0);

	callback();
}
