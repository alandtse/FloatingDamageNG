// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace FDNG::Fonts
{
	// Load a real TTF into the CURRENT ImGui context and make it the default
	// font, so damage text rasterizes crisply at large sizes instead of
	// upscaling the 13px embedded bitmap font. Called once per context (flat
	// context at D3D init; VR HUD context via the helper's style callback).
	void Load();
}
