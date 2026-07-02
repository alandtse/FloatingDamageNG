// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace FDNG::Renderer
{
	// Skyrim world units -> meters (1 game unit ≈ 1.428 cm).
	inline constexpr float kGameUnitToMeter = 0.01428f;

	// Install the D3D-init thunk and the HUDMenu::PostDisplay draw hook.
	void Install();

	// Connect to ImGuiVRHelper as a world-quad client (VR only; no-op on flat).
	// Call at kPostPostLoad.
	void Connect();

	// True only in VR when connected to a helper with world-quad support. When
	// false in VR, numbers are disabled entirely — never screen-space in an HMD.
	bool WorldQuadActive();
}
