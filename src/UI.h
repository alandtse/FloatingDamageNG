// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace FDNG::UI
{
	// Register the config + combat-stats pages with SKSE Menu Framework (SMF3).
	// No-op when the framework isn't installed — it is an optional dependency
	// resolved purely at runtime (GetProcAddress). Call at kDataLoaded.
	void Register();
}
