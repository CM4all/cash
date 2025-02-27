// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "DevCachefiles.hxx"
#include "Cull.hxx"
#include "event/Loop.hxx"
#include "event/PipeEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "config.h"

#ifdef HAVE_LIBSYSTEMD
#include "event/systemd/Watchdog.hxx"
#endif

#include <cstdint>

struct Config;

class Instance {
	EventLoop event_loop;
	ShutdownListener shutdown_listener{event_loop, BIND_THIS_METHOD(OnShutdown)};

#ifdef HAVE_LIBSYSTEMD
	Systemd::Watchdog systemd_watchdog{event_loop};
#endif

	UniqueFileDescriptor cache_fd, graveyard_fd;

	DevCachefiles dev_cachefiles;

	std::optional<Cull> cull;

	const uint_least8_t brun, frun;

	const bool culling_disabled;

public:
	explicit Instance(const Config &config);
	~Instance() noexcept;

	void Run();

private:
	void OnCull() noexcept;
	void StartCull();
	void OnCullComplete() noexcept;

	void OnShutdown() noexcept {
		cull.reset();
		dev_cachefiles.Disable();

#ifdef HAVE_LIBSYSTEMD
		systemd_watchdog.Disable();
#endif
	}
};
