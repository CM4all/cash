// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Walk.hxx"
#include "WHandler.hxx"
#include "WResult.hxx"
#include "event/Loop.hxx"
#include "event/ShutdownListener.hxx"
#include "event/uring/Manager.hxx"
#include "system/SetupProcess.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "time/ISO8601.hxx"
#include "util/PrintException.hxx"
#include "util/StringBuffer.hxx"

#include <fmt/core.h>

#include <optional>

struct Instance final : WalkHandler {
	EventLoop event_loop;
	ShutdownListener shutdown_listener{event_loop, BIND_THIS_METHOD(OnShutdown)};

	Uring::Manager uring{event_loop, 16384};

	std::optional<Walk> walk;

	Instance() {
		shutdown_listener.Enable();
	}

	void OnShutdown() noexcept {
		walk.reset();
		uring.SetVolatile();
	}

	// virtual methods from WalkHandler
	void OnWalkAncient([[maybe_unused]] WalkDirectory &directory,
			   std::string_view filename) noexcept override {
		fmt::print("ancient {:?}\n", filename);
	}

	void OnWalkFinished(WalkResult &&result) noexcept override {
		uring.SetVolatile();
		shutdown_listener.Disable();

		fmt::print("{} files, {} bytes\n", result.files.size(), result.total_bytes);

		for (const auto &file : result.files)
			fmt::print("{} {:10} {:?}\n",
				   FormatISO8601(std::chrono::system_clock::from_time_t(file.time.count())).c_str(),
				   file.size,
				   file.name);
	}
};

int
main(int argc, char **argv) noexcept
try {
	const char *path = ".";
	uint_least64_t collect_files = 64, collect_bytes = 1024 * 1024;

	if (argc > 4) {
		fmt::print(stderr, "Usage: RunWalk [PATH [COLLECT_FILES [COLLECT_BYTES]]]\n");
		return EXIT_FAILURE;
	}

	if (argc > 1)
		path = argv[1];

	if (argc > 2)
		collect_files = strtoul(argv[2], nullptr, 10);

	if (argc > 3)
		collect_bytes = strtoul(argv[3], nullptr, 10);

	SetupProcess();

	Instance instance;

	instance.walk.emplace(instance.uring,
			      collect_files, collect_bytes,
			      instance);
	instance.walk->Start(OpenDirectory(path));

	if (instance.walk)
		instance.event_loop.Run();

	return EXIT_SUCCESS;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
