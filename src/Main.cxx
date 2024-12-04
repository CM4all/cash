// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Instance.hxx"
#include "Config.hxx"
#include "system/Error.hxx"
#include "system/SetupProcess.hxx"
#include "io/Open.hxx"
#include "util/IterableSplitString.hxx"
#include "util/PrintException.hxx"
#include "util/SpanCast.hxx"
#include "util/StringSplit.hxx"
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

inline
Instance::Instance(const Config &config)
	:brun(config.brun), frun(config.frun),
	 culling_disabled(config.culling_disabled)
{
	dev_cachefiles.Open(OpenDevCachefiles(config).Release());
	dev_cachefiles.ScheduleRead();

	const auto fscache_fd = OpenPath(config.dir.c_str(), O_DIRECTORY);
	cache_fd = OpenPath(fscache_fd, "cache", O_DIRECTORY);
	graveyard_fd = OpenPath(fscache_fd, "graveyard", O_DIRECTORY);

	// TODO implement graveyeard reaper

	shutdown_listener.Enable();
}

inline
Instance::~Instance() noexcept
{
	dev_cachefiles.Close();
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

	cull.emplace(event_loop, uring, dev_cachefiles.GetFileDescriptor(),
		     cull_files, cull_bytes, BIND_THIS_METHOD(OnCullComplete));
	cull->Start(cache_fd);
}

inline void
Instance::OnCullComplete() noexcept
{
	cull.reset();

	dev_cachefiles.ScheduleRead();
}

inline void
Instance::OnDevCachefiles([[maybe_unused]] unsigned events) noexcept
{
	char buffer[1024];
	ssize_t nbytes = dev_cachefiles.GetFileDescriptor().Read(std::as_writable_bytes(std::span{buffer}));
	if (nbytes <= 0) {
		// TODO
		dev_cachefiles.CancelRead();
		return;
	}

	const std::string_view line{buffer, static_cast<std::size_t>(nbytes)};

	bool start_cull = false;
	for (const std::string_view i : IterableSplitString(line, ' ')) {
		const auto [name, value] = Split(i, '=');
		if (name == "cull"sv)
			start_cull = value != "0"sv;
	}

	if (start_cull && !cull && !culling_disabled)
		StartCull();
}

inline void
Instance::Run()
{
	event_loop.Run();
}

static int
Run()
{
	Instance instance{
		LoadConfigFile("/etc/cachefilesd.conf"),
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
	// TODO
	(void)argc;
	(void)argv;

	SetupProcess();

	return Run();
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
