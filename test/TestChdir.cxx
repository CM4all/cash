// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Chdir.hxx"
#include "event/Loop.hxx"
#include "co/InvokeTask.hxx"
#include "io/Open.hxx"
#include "util/SharedLease.hxx"

#include <gtest/gtest.h>

struct Completion {
	std::exception_ptr error;
	bool done = false;

	void Callback(std::exception_ptr &&_error) noexcept {
		ASSERT_FALSE(done);
		ASSERT_FALSE(error);

		error = std::move(_error);
		done = true;
	}

	void Start(Co::InvokeTask &invoke) noexcept {
		ASSERT_TRUE(invoke);
		invoke.Start(BIND_THIS_METHOD(Callback));
	}
};

struct WaitResult {
	SharedLease lease;
	bool resumed = false;

	[[nodiscard]]
	bool success() const noexcept {
		return static_cast<bool>(lease);
	}
};

static Co::InvokeTask
WaitForDirectory(Chdir &chdir, FileDescriptor directory, WaitResult &result) noexcept
{
	result.lease = co_await chdir.Add(directory);
	result.resumed = true;
}

TEST(Chdir, FailedDirectoryDoesNotBreakNextWaiter)
{
	EventLoop event_loop;
	Chdir chdir{event_loop};
	WaitResult failed, succeeded;

	auto valid_directory = OpenDirectory(".");

	auto failed_task = WaitForDirectory(chdir, FileDescriptor::Undefined(), failed);
	auto succeeded_task = WaitForDirectory(chdir, valid_directory, succeeded);

	Completion failed_completion, succeeded_completion;
	failed_completion.Start(failed_task);
	succeeded_completion.Start(succeeded_task);

	EXPECT_FALSE(failed.resumed);
	EXPECT_FALSE(succeeded.resumed);

	event_loop.Run();

	EXPECT_TRUE(failed_completion.done);
	EXPECT_FALSE(failed_completion.error);
	EXPECT_TRUE(failed.resumed);
	EXPECT_FALSE(failed.success());

	EXPECT_TRUE(succeeded_completion.done);
	EXPECT_FALSE(succeeded_completion.error);
	EXPECT_TRUE(succeeded.resumed);
	EXPECT_TRUE(succeeded.success());

	succeeded.lease = {};
	event_loop.Run();
}

TEST(Chdir, AddWhileLeaseHeld)
{
	EventLoop event_loop;
	Chdir chdir{event_loop};
	WaitResult first, second;

	auto directory = OpenDirectory(".");

	auto first_task = WaitForDirectory(chdir, directory, first);
	Completion first_completion;
	first_completion.Start(first_task);

	event_loop.Run();

	ASSERT_TRUE(first_completion.done);
	ASSERT_TRUE(first.success());

	/* the lease is still held: this one takes the fast path */
	auto second_task = WaitForDirectory(chdir, directory, second);
	Completion second_completion;
	second_completion.Start(second_task);

	EXPECT_TRUE(second_completion.done);
	EXPECT_FALSE(second_completion.error);
	EXPECT_TRUE(second.resumed);
	EXPECT_TRUE(second.success());

	first.lease = {};
	second.lease = {};
	event_loop.Run();

	/* the fast path must not have left a stale waiter behind */
	WaitResult third;
	auto third_task = WaitForDirectory(chdir, directory, third);
	Completion third_completion;
	third_completion.Start(third_task);

	event_loop.Run();

	EXPECT_TRUE(third_completion.done);
	EXPECT_TRUE(third.success());

	third.lease = {};
	event_loop.Run();
}

/**
 * A waiter which, after being resumed, adds two more waiters: one for
 * the same directory and one for another directory.
 */
struct NestedAdd {
	Chdir &chdir;
	FileDescriptor same, other;

	/**
	 * Release the outer lease before adding the nested waiters?
	 */
	bool release_first = false;

	WaitResult outer, same_result, other_result;

	Co::InvokeTask same_task, other_task;
	Completion same_completion, other_completion;

	NestedAdd(Chdir &_chdir, FileDescriptor _same, FileDescriptor _other) noexcept
		:chdir(_chdir), same(_same), other(_other) {}
};

static Co::InvokeTask
WaitAndAdd(NestedAdd &n) noexcept
{
	n.outer.lease = co_await n.chdir.Add(n.same);
	n.outer.resumed = true;

	if (n.release_first)
		n.outer.lease = {};

	n.same_task = WaitForDirectory(n.chdir, n.same, n.same_result);
	n.same_completion.Start(n.same_task);

	n.other_task = WaitForDirectory(n.chdir, n.other, n.other_result);
	n.other_completion.Start(n.other_task);
}

TEST(Chdir, AddFromResumedWaiter)
{
	EventLoop event_loop;
	Chdir chdir{event_loop};

	auto a = OpenDirectory(".");
	auto b = OpenDirectory(".");

	NestedAdd nested{chdir, a, b};
	auto nested_task = WaitAndAdd(nested);
	Completion nested_completion;
	nested_completion.Start(nested_task);

	/* still in the list while the nested waiters are added */
	WaitResult second;
	auto second_task = WaitForDirectory(chdir, a, second);
	Completion second_completion;
	second_completion.Start(second_task);

	event_loop.Run();

	EXPECT_TRUE(nested_completion.done);
	EXPECT_TRUE(nested.outer.success());
	EXPECT_TRUE(second.success());

	/* same directory: granted immediately */
	EXPECT_TRUE(nested.same_completion.done);
	EXPECT_TRUE(nested.same_result.success());

	/* other directory: waits for all leases to be released */
	EXPECT_FALSE(nested.other_result.resumed);

	nested.outer.lease = {};
	second.lease = {};
	nested.same_result.lease = {};
	event_loop.Run();

	EXPECT_TRUE(nested.other_completion.done);
	EXPECT_TRUE(nested.other_result.success());

	nested.other_result.lease = {};
	event_loop.Run();
}

TEST(Chdir, AddAfterReleaseFromResumedWaiter)
{
	EventLoop event_loop;
	Chdir chdir{event_loop};

	auto a = OpenDirectory(".");
	auto b = OpenDirectory(".");

	/* the outer lease is the only one, so releasing it abandons
	   the directory while Next() is still resuming */
	NestedAdd nested{chdir, a, b};
	nested.release_first = true;
	auto nested_task = WaitAndAdd(nested);
	Completion nested_completion;
	nested_completion.Start(nested_task);

	event_loop.Run();

	EXPECT_TRUE(nested_completion.done);
	EXPECT_TRUE(nested.outer.resumed);

	/* one directory at a time */
	ASSERT_NE(nested.same_result.resumed, nested.other_result.resumed);

	auto &first = nested.same_result.resumed ? nested.same_result : nested.other_result;
	auto &second = nested.same_result.resumed ? nested.other_result : nested.same_result;

	EXPECT_TRUE(first.success());
	first.lease = {};
	event_loop.Run();

	EXPECT_TRUE(nested.same_completion.done);
	EXPECT_TRUE(nested.other_completion.done);
	EXPECT_TRUE(second.success());

	second.lease = {};
	event_loop.Run();
}
