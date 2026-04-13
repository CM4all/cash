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
