// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Instance.hxx"
#include "Config.hxx"
#include "Options.hxx"
#include "system/Error.hxx"
#include "system/SetupProcess.hxx"
#include "io/FileAt.hxx"
#include "io/Open.hxx"
#include "io/uring/Queue.hxx"
#include "util/PrintException.hxx"
#include "util/SpanCast.hxx"
#include "config.h"

#include <fmt/core.h>

#ifdef HAVE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#ifdef HAVE_LIBCAP
#include "lib/cap/State.hxx"
#endif // HAVE_LIBCAP

#ifdef HAVE_LIBSYSTEMD
#endif

#include <optional>
#include <string>

#include <errno.h>
#include <fcntl.h> // for O_RDWR
#include <string.h> // for strerror()
#include <sys/statvfs.h>

using std::string_view_literals::operator""sv;

static UniqueFileDescriptor
OpenDevCachefiles(const Config &config)
{
	UniqueFileDescriptor fd;
	if (!fd.Open("/dev/cachefiles", O_RDWR))
		throw MakeErrno("Failed to open /dev/cachefiles");

	for (const auto &line : config.kernel_config)
		fd.FullWrite(AsBytes(line));

	fd.FullWrite(AsBytes("bind"sv));

	return fd;
}

/**
 * Add 2% to the configured BRUN / FRUN values to compensate for files
 * being added while we're culling.
 *
 * If each Cull operation attempts to reach exactly BRUN / FRUN, it
 * will fail to meet that goal because new files are being added
 * during the Cull, and another Cull will be started right after that,
 * which will again fail to meet the goal, leaving the daemon in an
 * endless culling loop.
 */
static constexpr uint_least8_t RUN_PERCENT_OFFSET = 2;

inline
Instance::Instance(const Config &config)
	:dev_cachefiles(event_loop, OpenDevCachefiles(config), BIND_THIS_METHOD(OnCull)),
	 brun(config.brun + RUN_PERCENT_OFFSET),
	 frun(config.frun + RUN_PERCENT_OFFSET),
	 culling_disabled(config.culling_disabled)
{
	event_loop.EnableUring(16384, IORING_SETUP_SINGLE_ISSUER|IORING_SETUP_COOP_TASKRUN);
	event_loop.GetUring()->SetMaxWorkers(16, 16);

	const auto fscache_fd = OpenPath(config.dir.c_str(), O_DIRECTORY);
	cache_fd = OpenPath({fscache_fd, "cache"}, O_DIRECTORY);
	graveyard_fd = OpenPath({fscache_fd, "graveyard"}, O_DIRECTORY);

	// TODO implement graveyeard reaper

	shutdown_listener.Enable();
	dev_cachefiles.Enable();
}

inline
Instance::~Instance() noexcept
{
}

inline void
Instance::StartCull()
{
	uint_least64_t cull_files = 0;
	uint_least64_t cull_bytes = 1024 * 1024;

	struct statvfs s;
	if (fstatvfs(cache_fd.Get(), &s) == 0) {
		uint_least64_t target_files = (s.f_files * frun + 99) / 100;
		if (target_files > s.f_ffree)
			cull_files = target_files - s.f_ffree;

		uint_least64_t target_blocks = (s.f_blocks * brun + 99) / 100;
		if (target_blocks > s.f_bfree)
			cull_bytes = static_cast<uint_least64_t>(target_blocks - s.f_bfree) * s.f_bsize;
	} else {
		fmt::print(stderr, "fstatvfs() failed: %s\n", strerror(errno));
	}

	fmt::print(stderr, "Cull: start files={} bytes={}\n", cull_files, cull_bytes);

	cull.emplace(event_loop, *event_loop.GetUring(),
		     dev_cachefiles,
		     cull_files, cull_bytes, BIND_THIS_METHOD(OnCullComplete));
	cull->Start(cache_fd);
}

inline void
Instance::OnCullComplete() noexcept
{
	cull.reset();

	/* re-enable polling /dev/cachefiles */
	dev_cachefiles.Enable();
}

inline void
Instance::OnCull() noexcept
{
	/* disable polling /dev/cachefiles while we're culling */
	dev_cachefiles.Disable();

	if (!cull && !culling_disabled)
		StartCull();
}

inline void
Instance::Run()
{
	event_loop.Run();
}

static int
Run(const Options &options)
{
	Instance instance{
		LoadConfigFile(options.configfile),
	};

#ifdef HAVE_LIBCAP
	/* drop all capabilities, we don't need them anymore */
	CapabilityState::Empty().Install();
#endif // HAVE_LIBCAP

#ifdef HAVE_LIBSYSTEMD
	/* tell systemd we're ready */
	sd_notify(0, "READY=1");
#endif

	instance.Run();

	return EXIT_SUCCESS;
}

int
main(int argc, char **argv) noexcept
try {
	const auto options = ParseCommandLine(argc, argv);

	SetupProcess();

	return Run(options);
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
