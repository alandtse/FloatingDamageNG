// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "Capture.h"
#include "Settings.h"

// Pure geometry shared by the in-world renderer and the SMF preview (which
// cannot share ImGui draw calls across the cimgui/imgui split, but must not
// drift on the math). Everything here is ImGui-free.
namespace FDNG
{
	// Origin-marker paddings around the text block, in panel pixels. The
	// whole marker must fit inside the reported block extent — in VR anything
	// outside it falls off the billboard quad.
	inline constexpr float kBoxPadX = 4.0f;
	inline constexpr float kBoxPadY = 3.0f;
	inline constexpr float kUnderlineGap = 2.0f;

	struct StyleMetrics
	{
		float padX{ 0.0f };
		float padTop{ 0.0f };
		float padBottom{ 0.0f };
	};

	constexpr StyleMetrics ComputeStyleMetrics(OriginStyle a_style, float a_thickness)
	{
		switch (a_style) {
		case OriginStyle::kBox:
			return { kBoxPadX + a_thickness, kBoxPadY + a_thickness, kBoxPadY + a_thickness };
		case OriginStyle::kUnderline:
			// The 1 px legibility outline needs a hair of slack too.
			return { 1.0f, 1.0f, kUnderlineGap + a_thickness };
		case OriginStyle::kOutline:
		case OriginStyle::kNone:
		default:
			// The ring offsets the glyphs by the full thickness in every
			// direction; without pads it clips at the block edges.
			return { a_thickness, a_thickness, a_thickness };
		}
	}

	// 8-direction ring offsets for outlined text, as {x, y} factors of the
	// thickness (unit circle: cardinal + diagonal at 1/sqrt2).
	inline constexpr float kRingOffsets[8][2] = {
		{ 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f },
		{ 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { 0.7071f, -0.7071f }, { -0.7071f, -0.7071f }
	};

	// The mitigation subtext grows with the resisted share so a
	// mostly-resisted hit reads at a glance. Both the block drawer and the
	// VR packer's width reservation must use the same ratio.
	inline float SubtextRatio(float a_amount, float a_mitigated)
	{
		if (a_mitigated > 0.0f && a_amount > 0.0f) {
			return 0.40f + 0.35f * (a_mitigated / (a_mitigated + a_amount));
		}
		return 0.55f;
	}

	// The user-themed color for a damage kind (0xRRGGBB) — shared by the
	// in-world renderer and the stats page's meter bars.
	inline std::uint32_t KindRgb(const Settings& a_settings, DamageKind a_kind)
	{
		switch (a_kind) {
		case DamageKind::kFire:
			return a_settings.colorFire;
		case DamageKind::kFrost:
			return a_settings.colorFrost;
		case DamageKind::kShock:
			return a_settings.colorShock;
		case DamageKind::kPoison:
			return a_settings.colorPoison;
		case DamageKind::kMagic:
			return a_settings.colorMagic;
		case DamageKind::kHealing:
			return a_settings.colorHealing;
		case DamageKind::kMagickaDrain:
			return a_settings.colorMagickaDamage;
		case DamageKind::kStaminaDrain:
			return a_settings.colorStaminaDamage;
		default:
			return a_settings.colorPhysical;
		}
	}
}
