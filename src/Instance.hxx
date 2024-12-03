// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Cull.hxx"
#include "event/Loop.hxx"
#include "event/PipeEvent.hxx"
#include "event/ShutdownListener.hxx"
#include "event/uring/Manager.hxx"
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

	Uring::Manager uring{event_loop, 16384};

	UniqueFileDescriptor cache_fd, graveyard_fd;

	PipeEvent dev_cachefiles{event_loop, BIND_THIS_METHOD(OnDevCachefiles)};

	std::optional<Cull> cull;

	const uint_least8_t brun, frun;

public:
	explicit Instance(const Config &config);
	~Instance() noexcept;

	void Run();

private:
	void OnDevCachefiles(unsigned events) noexcept;

	void StartCull();
	void OnCullComplete() noexcept;

	void OnShutdown() noexcept {
		cull.reset();
		dev_cachefiles.Cancel();
		uring.SetVolatile();

#ifdef HAVE_LIBSYSTEMD
		systemd_watchdog.Disable();
#endif
	}
};
