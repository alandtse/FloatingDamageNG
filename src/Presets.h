// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "Settings.h"

// Shareable motion-effect presets. The built-ins (Float/Arc/Radial/Fireworks)
// and any JSON files a user drops in Data/SKSE/Plugins/FloatingDamageNG/Presets
// are surfaced through one list, so a custom effect is authored in-game (live
// preview), saved to a file, and traded like any other mod asset. No new
// dependency — the format is plain JSON via the already-vendored nlohmann.
namespace FDNG::Presets
{
	// One effect: the launch path plus how successive hits fan out. Mirrors the
	// compiled EffectPreset but owns its name so loaded and built-in entries
	// share a type.
	struct Effect
	{
		std::string name;
		bool builtIn{ false };
		MotionProfile motion;
		SpreadPattern spread{ SpreadPattern::kAlternate };
		float spawnAngleDeg{ 0.0f };
	};

	// Built-ins first, then user files (alphabetical). Cached; call Reload to
	// rescan the directory after files change on disk.
	const std::vector<Effect>& All();
	void Reload();

	// Write an effect as <name>.json to the user preset directory (creating it).
	// Returns false on an unsafe/empty name or a write failure.
	bool Save(const Effect& a_effect);
}
