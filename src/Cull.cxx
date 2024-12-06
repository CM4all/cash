// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Cull.hxx"
#include "Walk.hxx"
#include "io/uring/CoOperation.hxx"
#include "system/Error.hxx"
#include "co/InvokeTask.hxx"
#include "util/DeleteDisposer.hxx"

#include <cassert>

#include <fmt/core.h> // TODO

inline Co::InvokeTask
Cull::CullFile(WalkDirectoryRef directory, std::string name,
	       uint_least64_t size) noexcept
{
	const auto chdir_lease = co_await chdir.Add(directory->fd);
	if (!chdir_lease) {
		++n_errors;
		co_return;
	}

	DevCachefiles::Buffer buffer;
	const auto w = dev_cachefiles.FormatCullFile(buffer, name);
	if (w.data() == nullptr) {
		++n_errors;
		co_return;
	}

	const auto nbytes = co_await Uring::CoTryWrite(uring,
						       dev_cachefiles.GetFileDescriptor(),
						       w, 0);
	switch (dev_cachefiles.CheckCullFileResult(name, nbytes)) {
	case DevCachefiles::CullResult::SUCCESS:
		++n_deleted_files;
		n_deleted_bytes += size;
		break;

	case DevCachefiles::CullResult::BUSY:
		++n_busy;
		break;

	case DevCachefiles::CullResult::ERROR:
		++n_errors;
		break;
	}
}

class Cull::Operation final
	: public IntrusiveListHook<>
{
	Cull &cull;

	Co::InvokeTask task;

public:
	Operation(Cull &_cull, Co::InvokeTask &&_task) noexcept
		:cull(_cull), task(std::move(_task)) {}

	void Start() noexcept {
		task.Start(BIND_THIS_METHOD(OnComplete));
	}

private:
	void OnComplete(std::exception_ptr error) noexcept {
		(void)error; // TODO

		cull.OperationFinished(*this);
	}
};

Cull::Cull(EventLoop &event_loop, Uring::Queue &_uring,
	   FileDescriptor _dev_cachefiles,
	   uint_least64_t _cull_files, std::size_t _cull_bytes,
	   Callback _callback)
	:uring(_uring), dev_cachefiles(_dev_cachefiles),
	 callback(_callback),
	 walk(new Walk(_uring, _cull_files, _cull_bytes, *this)),
	 chdir(event_loop),
	 defer_start(event_loop, BIND_THIS_METHOD(OnDeferredStart))
{
	assert(callback);
}

Cull::~Cull() noexcept
{
	operations.clear_and_dispose(DeleteDisposer{});
	new_operations.clear_and_dispose(DeleteDisposer{});
}

void
Cull::Start(FileDescriptor root_fd)
{
	walk->Start(root_fd);
}

void
Cull::OnWalkAncient(WalkDirectory &directory,
		    std::string &&filename,
		    uint_least64_t size) noexcept
{
	AddOperation(CullFile(WalkDirectoryRef{directory}, std::move(filename), size));
}

void
Cull::OnWalkFinished(WalkResult &&result) noexcept
{
	fmt::print(stderr, "Cull: delete {} files, {} bytes\n",
		   result.files.size(), result.total_bytes);

	result.files.clear_and_dispose([&](WalkResult::File *file){
#ifndef NDEBUG
		result.total_bytes -= file->size;
#endif

		AddOperation(CullFile(std::move(file->parent), std::move(file->name), file->size));
		delete file;
	});

	assert(result.total_bytes == 0);

	walk.reset();

	if (!new_operations.empty())
		defer_start.Schedule();
	else if (operations.empty())
		Finish();
}

inline void
Cull::OnDeferredStart() noexcept
{
	assert(!new_operations.empty());

	new_operations.clear_and_dispose([this](auto *op){
		operations.push_back(*op);
		op->Start();
	});
}

inline void
Cull::AddOperation(Co::InvokeTask &&task) noexcept
{
	auto *op = new Operation(*this, std::move(task));
	new_operations.push_back(*op);
	defer_start.Schedule();
}

void
Cull::OperationFinished(Operation &op) noexcept
{
	assert(!operations.empty());

	operations.erase_and_dispose(operations.iterator_to(op), DeleteDisposer{});
	if (!walk && operations.empty() && new_operations.empty())
		Finish();
}

void
Cull::Finish() noexcept
{
	fmt::print(stderr, "Cull: deleted {} files, {} bytes; {} in use; {} errors\n", n_deleted_files, n_deleted_bytes, n_busy, n_errors);
	callback();
}
