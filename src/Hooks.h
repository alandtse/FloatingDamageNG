// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace FDNG::Hooks
{
	// Install the damage hooks (HandleHealthDamage vfuncs + hit-dispatch thunk).
	// Call at kPostPostLoad; requires SKSE::AllocTrampoline beforehand.
	void Install();
}
