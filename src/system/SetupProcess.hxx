// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef BENG_PROXY_SETUP_PROCESS_HXX
#define BENG_PROXY_SETUP_PROCESS_HXX

/**
 * Set up the current process by applying some common settings.
 *
 * - ignore SIGPIPE
 * - increase timer slack
 * - disable pthread cancellation
 */
void
SetupProcess();

/**
 * Set up a forked child process.
 */
void
PostFork();

#endif
