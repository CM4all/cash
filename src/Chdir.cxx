// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Chdir.hxx"

#include <unistd.h>

Chdir::Chdir(EventLoop &event_loop) noexcept
	:defer_next(event_loop, BIND_THIS_METHOD(Next))
{
}

Chdir::~Chdir() noexcept
{
	/* revert to "/" so we don't occupy an arbitrary directory
	   (that would prevent unmounting, for example) */
	chdir("/");
}

inline void
Chdir::Next() noexcept
{
	/* if there is a valid "current" directory, we don't need to
	   fchdir() again */
	const bool need_chdir = current == map.end();

	if (current == map.end()) {
		/* find the first non-empty list, skipping canceled
		   directories */
		while (true) {
			current = map.begin();
			if (current == map.end())
				/* no waiters left - stop here */
				return;

			if (!current->second.empty())
				/* this is not empty - use it */
				break;

			/* all waiters have been canceled; remove this
			   directory; the FileDescriptor may have been closed
			   already, but this doesn't matter, we don't use it
			   anyway */
			current = map.erase(current);
		}
	}

	auto &list = current->second;

	if (need_chdir && fchdir(current->first.Get()) < 0) {
		// error

		/* move the list to the stack and erase the failed
		   directory from the map */
		auto tmp = std::move(list);

		map.erase(current);
		current = map.end();

		/* schedule the next fchdir() after the callback loop
		   below has finished */
		defer_next.Schedule();

		list.clear_and_dispose([](auto *i){
			i->OnChdirError();
		});
	} else {
		/* success: create a lease first; this is kept alive
		   until all waiters have been invoked to avoid
		   OnAbandoned() to be called in the middle of this
		   loop */
		SharedLease lease{*this};

		while (true) {
			auto &i = list.pop_front();
			if (list.empty()) {
				/* the last waiter inherits this
				   lease, so it won't be released at
				   the end of this scope */
				i.OnChdir(std::move(lease));
				break;
			}

			/* all other waiters get a copy of the
			   lease */
			i.OnChdir(lease);
		}
	}
}

void
Chdir::Add(FileDescriptor directory, ChdirWaiter &w) noexcept
{
	map[directory].push_back(w);

	if (current == map.end() || current->first == directory)
		defer_next.Schedule();
}

void
Chdir::OnAbandoned() noexcept
{
	assert(current != map.end());

	map.erase(current);
	current = map.end();

	defer_next.Schedule();
}