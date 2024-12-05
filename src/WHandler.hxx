// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

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
				   std::string_view filename) noexcept = 0;

	/**
	 * The #Walk has finished completely.  This method is allowed
	 * destruct the #Walk instance (which will invalidate the
	 * #result).
	 */
	virtual void OnWalkFinished(WalkResult &&result) noexcept = 0;
};
