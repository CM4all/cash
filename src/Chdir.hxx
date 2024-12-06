// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/DeferEvent.hxx"
#include "io/FileDescriptor.hxx"
#include "co/Compat.hxx"
#include "util/IntrusiveList.hxx"
#include "util/SharedLease.hxx"

#include <map>

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

	struct Awaitable : IntrusiveListHook<IntrusiveHookMode::AUTO_UNLINK> {
		using List = IntrusiveList<Awaitable>;

		Chdir &parent;

		std::coroutine_handle<> continuation;

		SharedLease lease;

		explicit Awaitable(Chdir &_parent, List &list) noexcept
			:parent(_parent)
		{
			list.push_back(*this);

			if (parent.current != parent.map.end() &&
			    &parent.current->second == &list)
			 	lease = parent;
		}

		Awaitable(const Awaitable &) = delete;
		Awaitable &operator=(const Awaitable &) = delete;

		[[nodiscard]]
		bool await_ready() const noexcept {
			assert(is_linked());
			assert(!continuation);

			return (bool)lease;
		}

		void await_suspend(std::coroutine_handle<> _continuation) noexcept {
			assert(is_linked());
			assert(!continuation);
			assert(_continuation);
			assert(!_continuation.done());

			continuation = _continuation;
		}

		SharedLease await_resume() noexcept {
			assert(!is_linked());
			assert((bool)lease == (parent.current != parent.map.end()));

			return std::move(lease);
		}
	};

	using WaiterMap = std::map<FileDescriptor, Awaitable::List, FileDescriptorCompare>;

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
	Awaitable Add(FileDescriptor directory) noexcept;

private:
	void Next() noexcept;

	// virtual methods from SharedAnchor
	void OnAbandoned() noexcept override;
};
