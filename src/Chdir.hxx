// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/DeferEvent.hxx"
#include "io/FileDescriptor.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SharedLease.hxx"

#include <map>

/**
 * Handler class for #Chdir.
 */
class ChdirWaiter {
	IntrusiveListHook<IntrusiveHookMode::AUTO_UNLINK> chdir_siblings;

public:
	using List = IntrusiveList<ChdirWaiter, IntrusiveListMemberHookTraits<&ChdirWaiter::chdir_siblings>>;

	/**
	 * The current working directory is now the one passed to
	 * Chdir::Add().  This method may now perform I/O, even after
	 * this method has returned.  When all I/O in this directory
	 * is complete, release the #lease.
	 */
	virtual void OnChdir(SharedLease lease) noexcept = 0;

	/**
	 * An (unspecified) error has occurred and the directory could
	 * not be changed.
	 */
	virtual void OnChdirError() noexcept = 0;
};

/**
 * Provides an optimization for changing the current working directory
 * using fchdir(): multiple callers can ask to change the working
 * directory and this class groups all callers with the same
 * directory.  It calls them back using the #ChdirWaiter interface,
 * waits for all leases to be released, and then goes on with the next
 * directory.
 */
class Chdir final : SharedAnchor {
	DeferEvent defer_next;

	struct FileDescriptorCompare {
		constexpr bool operator()(FileDescriptor a, FileDescriptor b) const noexcept {
			return a.Get() < b.Get();
		}
	};

	using WaiterMap = std::map<FileDescriptor, ChdirWaiter::List, FileDescriptorCompare>;

	/**
	 * Maps directory file descriptors to a list of waiters.  The
	 * file descriptors are owned by the waiters.
	 */
	WaiterMap map;

	/**
	 * If this is not end(), then this points to the current
	 * working directory and there are unreleased leases.
	 */
	WaiterMap::iterator current = map.end();

public:
	explicit Chdir(EventLoop &event_loop) noexcept;
	~Chdir() noexcept;

	Chdir(const Chdir &) = delete;
	Chdir &operator=(const Chdir &) = delete;

	/**
	 * Schedule a fchdir() call.
	 *
	 * @param directory where to change to; must remain valid
	 * until the #ChdirWaiter has been called
	 * @param w the waiter that will be called back; it may be
	 * canceled by simply destructing the #ChdirWaiter instance
	 */
	void Add(FileDescriptor directory, ChdirWaiter &w) noexcept;

private:
	void Next() noexcept;

	// virtual methods from SharedAnchor
	void OnAbandoned() noexcept override;
};
