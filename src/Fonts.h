// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "Capture.h"  // DamageKind

struct ImFont;

namespace FDNG::Fonts
{
	// Load a real TTF into the CURRENT ImGui context and make it the default
	// font, so damage text rasterizes crisply at large sizes instead of
	// upscaling the 13px embedded bitmap font. Called once per context (flat
	// context at D3D init; VR HUD context via the helper's style callback).
	void Load();

	// Selectable fonts for the config UI: {display name, absolute path}, from
	// the drop-in folder, the mod's font folder, and the Windows font
	// directory. Cached; a font change applies on the next context load
	// (game restart).
	const std::vector<std::pair<std::string, std::string>>& Available();

	// The font a given damage kind should draw with in the CURRENT ImGui
	// context: its per-kind override (fontByKind) if loaded, else the default
	// font. Per-context (flat and VR keep separate atlases).
	ImFont* ForKind(DamageKind a_kind);
}
