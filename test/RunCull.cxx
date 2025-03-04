// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Cull.hxx"
#include "DevCachefiles.hxx"
#include "event/Loop.hxx"
#include "event/ShutdownListener.hxx"
#include "system/Error.hxx"
#include "system/SetupProcess.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "time/ISO8601.hxx"
#include "util/PrintException.hxx"
#include "util/SpanCast.hxx"
#include "util/StringBuffer.hxx"

#include <fmt/core.h>
#include <liburing.h>

#include <optional>

using std::string_view_literals::operator""sv;

static UniqueFileDescriptor
OpenDevCachefiles()
{
	UniqueFileDescriptor fd;
	if (!fd.Open("/dev/cachefiles", O_RDWR))
		throw MakeErrno("Failed to open /dev/cachefiles");

	fd.FullWrite(AsBytes("dir /var/cache/fscache"sv));
	fd.FullWrite(AsBytes("tag mycache"sv));
	fd.FullWrite(AsBytes("bind"sv));

	return fd;
}

struct Instance final {
	EventLoop event_loop;
	ShutdownListener shutdown_listener{event_loop, BIND_THIS_METHOD(OnShutdown)};

	DevCachefiles dev_cachefiles{event_loop, OpenDevCachefiles(), BIND_THIS_METHOD(OnCull)};

	std::optional<Cull> cull;

	Instance(uint_least64_t cull_files, uint_least64_t cull_bytes)
	{
		event_loop.EnableUring(16384, IORING_SETUP_SINGLE_ISSUER|IORING_SETUP_COOP_TASKRUN);
		cull.emplace(event_loop, *event_loop.GetUring(),
			     dev_cachefiles,
			     cull_files, cull_bytes,
			     BIND_THIS_METHOD(OnCullComplete));
	}

	void OnShutdown() noexcept {
		cull.reset();
	}

	void OnCull() noexcept {}

	void OnCullComplete() noexcept {
		cull.reset();
		shutdown_listener.Disable();
	}
};

int
main(int argc, char **argv) noexcept
try {

	if (argc != 4) {
		fmt::print(stderr, "Usage: RunCull PATH CULL_FILES CULL_BYTES\n");
		return EXIT_FAILURE;
	}

	const char *path = argv[1];
	const uint_least64_t cull_files = strtoul(argv[2], nullptr, 10);
	const uint_least64_t cull_bytes = strtoul(argv[3], nullptr, 10);

	SetupProcess();

	Instance instance{cull_files, cull_bytes};

	instance.cull->Start(OpenDirectory(path));

	if (instance.cull)
		instance.event_loop.Run();

	return EXIT_SUCCESS;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
