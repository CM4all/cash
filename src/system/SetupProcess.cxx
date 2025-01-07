// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SetupProcess.hxx"

#include <sys/signal.h>
#include <pthread.h>

void
SetupProcess()
{
	signal(SIGPIPE, SIG_IGN);

	/* reduce glibc's thread cancellation overhead */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);
}

static void
UnignoreSignals()
{
	/* restore all signals which were set to SIG_IGN */
	static constexpr int signals[] = {
		SIGPIPE,
	};

	for (auto i : signals)
		signal(i, SIG_DFL);
}

static void
UnblockSignals()
{
	sigset_t mask;
	sigfillset(&mask);
	sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}

void
PostFork()
{
	UnignoreSignals();
	UnblockSignals();
}
