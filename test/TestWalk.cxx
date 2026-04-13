// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Walk.hxx"
#include "WHandler.hxx"
#include "event/Loop.hxx"
#include "io/FileAt.hxx"
#include "io/Open.hxx"
#include "io/RecursiveDelete.hxx"
#include "io/Temp.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/ScopeExit.hxx"

#include <gtest/gtest.h>
#include <liburing.h>

#include <array>
#include <memory>

#include <fcntl.h> // for O_PATH

struct WalkCompletion final : WalkHandler {
	EventLoop &event_loop;
	bool finished = false;
	std::size_t ancient = 0;
	std::size_t files = static_cast<std::size_t>(-1);
	uint_least64_t total_bytes = static_cast<uint_least64_t>(-1);

	explicit WalkCompletion(EventLoop &_event_loop) noexcept
		:event_loop(_event_loop) {}

	void OnWalkAncient([[maybe_unused]] WalkDirectory &directory,
			   [[maybe_unused]] std::string &&filename,
			   [[maybe_unused]] uint_least64_t size) noexcept override {
		++ancient;
	}

	void OnWalkFinished(WalkResult &&result) noexcept override {
		finished = true;
		files = result.files.size();
		total_bytes = result.total_bytes;
		event_loop.Break();
	}
};

TEST(Walk, EmptyRootFinishes)
{
	const auto tmp = OpenTmpDir(O_PATH);
	const auto directory_name = MakeTempDirectory(tmp, 0700);
	AtScopeExit(&tmp, &directory_name) {
		RecursiveDelete({tmp, directory_name});
	};

	const auto directory = OpenDirectoryPath({tmp, directory_name});

	EventLoop event_loop;
	event_loop.EnableUring(16384, IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN);

	WalkCompletion completion{event_loop};
	auto walk = std::make_unique<Walk>(*event_loop.GetUring(), 64, 1024 * 1024, completion);
	walk->Start(directory);

	event_loop.Run();

	EXPECT_TRUE(completion.finished);
	EXPECT_EQ(completion.ancient, 0u);
	EXPECT_EQ(completion.files, 0u);
	EXPECT_EQ(completion.total_bytes, 0u);
}
