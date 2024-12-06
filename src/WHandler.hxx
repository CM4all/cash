// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-or-later
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>
#include <string>

class FileDescriptor;
struct WalkDirectory;
struct WalkResult;

/**
 * Handler class for #Walk.
 */
class WalkHandler {
public:
	/**
	 * An "ancient" file was found (that should be culled
         * unconditionally).
	 */
	virtual void OnWalkAncient(WalkDirectory &directory,
				   std::string &&filename,
				   uint_least64_t size) noexcept = 0;

	/**
	 * The #Walk has finished completely.  This method is allowed
	 * destruct the #Walk instance (which will invalidate the
	 * #result).
	 */
	virtual void OnWalkFinished(WalkResult &&result) noexcept = 0;
};
