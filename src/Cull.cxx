// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Cull.hxx"
#include "Walk.hxx"
#include "io/uring/Operation.hxx"
#include "io/uring/Queue.hxx"
#include "util/DeleteDisposer.hxx"

#include <cassert>
#include <memory>

#include <fmt/core.h> // TODO

class Cull::CullFileOperation final
	: public IntrusiveListHook<>, ChdirWaiter, Uring::Operation
{
	Cull &cull;

	const WalkDirectoryRef directory;

	const std::string name;

	const uint_least64_t size;

	SharedLease chdir_lease;

	DevCachefiles::Buffer buffer;

public:
	CullFileOperation(Cull &_cull, WalkDirectory &_directory,
			  std::string &&_name, uint_least64_t _size) noexcept
		:cull(_cull), directory(_directory),
		 name(std::move(_name)),
		 size(_size) {}

	CullFileOperation(Cull &_cull, WalkResult::File &&file) noexcept
		:CullFileOperation(_cull, *file.parent,
				   std::move(file.name), file.size) {}

	void Start() noexcept {
		cull.chdir.Add(directory->fd, *this);
	}

	// virtual methods from ChdirWaiter
	void OnChdir(SharedLease lease) noexcept override;
	void OnChdirError() noexcept override;

	// virtual methods from Uring::Operation
	void OnUringCompletion(int res) noexcept override;
};

void
Cull::CullFileOperation::OnChdir(SharedLease lease) noexcept
{
	chdir_lease = std::move(lease);

	const auto w = DevCachefiles::FormatCullFile(buffer, name);
	if (w.data() == nullptr) {
		++cull.n_errors;
		cull.OperationFinished(*this);
		return;
	}

	auto &s = cull.uring.RequireSubmitEntry();
	io_uring_prep_write(&s, cull.dev_cachefiles.GetFileDescriptor().Get(),
			    w.data(), w.size(), 0);
	cull.uring.Push(s, *this);
}

void
Cull::CullFileOperation::OnChdirError() noexcept
{
	++cull.n_errors;
	cull.OperationFinished(*this);
}

void
Cull::CullFileOperation::OnUringCompletion(int res) noexcept
{
	switch (cull.dev_cachefiles.CheckCullFileResult(name, res)) {
	case DevCachefiles::CullResult::SUCCESS:
		++cull.n_deleted_files;
		cull.n_deleted_bytes += size;
		break;

	case DevCachefiles::CullResult::BUSY:
		++cull.n_busy;
		break;

	case DevCachefiles::CullResult::ERROR:
		++cull.n_errors;
		break;
	}

	cull.OperationFinished(*this);
}

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
	auto *op = new CullFileOperation(*this, directory, std::move(filename), size);
	new_operations.push_back(*op);
	defer_start.Schedule();
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

		auto *op = new CullFileOperation(*this, std::move(*file));
		delete file;
		new_operations.push_back(*op);
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

void
Cull::OperationFinished(CullFileOperation &op) noexcept
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
