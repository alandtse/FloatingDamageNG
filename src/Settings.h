// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace FDNG
{
	enum class KinematicProfile : std::int32_t
	{
		kFloat = 0,
		kArc = 1,
		kRadial = 2
	};

	// How a number marks its origin (whose fight it is).
	enum class OriginStyle : std::int32_t
	{
		kOutline = 0,    // text outline in the origin color
		kUnderline = 1,  // thin black outline + origin-colored underline bar
		kBox = 2         // thin black outline + origin-colored border box
	};

	struct Settings
	{
		static Settings* GetSingleton();

		void Load();
		void Save() const;       // write current values back to the INI (used by the SMF config UI)
		void ResetToDefaults();  // in-memory only; Save() to persist

		// [CoreFilters]
		bool showPlayerDamageDealt{ true };
		bool showFollowerDamageDealt{ true };
		bool showNPCOnNPCDamage{ true };     // heavily attenuated + distance-culled anyway
		bool showPlayerDamageTaken{ true };  // suppressed while in first person
		bool showHealing{ true };
		bool showMagickaDamage{ false };  // hostile-only (self costs filtered)
		bool showStaminaDamage{ false };  // hostile-only (self costs filtered)
		float maxVisibilityRadiusMeters{ 20.0f };
		int maxConcurrentQuads{ 40 };

		// [DynamicSizing]
		float baseFontScale{ 1.0f };
		float logScaleModifier{ 0.25f };
		float maxFontScaleCeiling{ 1.6f };

		// [KinematicProfiles]
		KinematicProfile profile{ KinematicProfile::kArc };
		float globalSpeedMultiplier{ 1.1f };
		float quadLifetimeSeconds{ 1.5f };

		// [Behavior]
		bool showMitigation{ true };
		float minDamageToShow{ 1.0f };
		float minHealToShow{ 5.0f };           // accumulation threshold; filters natural regen trickle
		float dotAccumulationWindow{ 0.35f };  // merge same victim+type events younger than this

		// [Style] — how a number's origin (whose fight it is) is marked. The
		// fill color always encodes the damage kind, so origin uses a
		// secondary channel in the relationship colors below.
		OriginStyle originStyle{ OriginStyle::kOutline };
		float styleThickness{ 2.0f };  // outline / underline / box border, panel px

		// [Colors] 0xRRGGBB (alpha applied at draw time)
		std::uint32_t colorPhysical{ 0xFF3B30 };
		std::uint32_t colorCritical{ 0xFFCC00 };
		std::uint32_t colorBlocked{ 0x4A4A4A };
		std::uint32_t colorFire{ 0xFF9500 };
		std::uint32_t colorFrost{ 0x5AC8FA };
		std::uint32_t colorShock{ 0xAF52DE };
		std::uint32_t colorPoison{ 0x4CD964 };
		std::uint32_t colorMagic{ 0xC7B8E8 };  // untyped magic
		std::uint32_t colorHealing{ 0x34C759 };
		std::uint32_t colorMagickaDamage{ 0x4169E1 };  // royal blue
		std::uint32_t colorStaminaDamage{ 0x2E8B57 };  // sea green
		// Origin marker colors (relationship palette: incoming red, ally
		// blue, unrelated gray; your own hits stay neutral black).
		std::uint32_t colorOriginPlayer{ 0x000000 };
		std::uint32_t colorOriginTaken{ 0x8C1414 };
		std::uint32_t colorOriginFollower{ 0x19508C };
		std::uint32_t colorOriginNPC{ 0x3C3C3C };

		// [Locational] — engine-derived hit location (projectile impact node),
		// mod-agnostic: works in vanilla and with any locational-damage mod.
		struct LocationTag
		{
			std::regex pattern;  // matched against the struck skeleton node name
			std::string label;   // e.g. "HEADSHOT"
		};
		bool showHitLocation{ true };
		std::vector<LocationTag> locationTags;  // built in Load(); first match wins
		// Show the implied external multiplier (e.g. x2.5) when a projectile
		// hit's totalDamage exceeds its physical+crit baseline — the pattern
		// locational-damage mods use. Heuristic; projectile hits only.
		bool showAmplification{ true };
		float amplificationThreshold{ 1.4f };

		// [FirstPerson] — player-received numbers (damage and heals) in first
		// person: pinned to a screen spot on flat, anchored ~1 m ahead at
		// chest height in VR. Off = suppressed entirely in first person.
		bool showFirstPersonNumbers{ true };
		float firstPersonX{ 0.5f };   // flat pin, fraction of screen width
		float firstPersonY{ 0.72f };  // flat pin, fraction of screen height

		// [Font]
		std::string fontPath;  // empty = auto (mod override, then Windows bold fonts)

		// [Analytics]
		bool enableCombatLog{ true };       // master switch for the analytics module
		bool writeLogToDisk{ true };        // append session reports to FloatingDamageNG-combat.log
		bool enableLiveDPSWindow{ false };  // opt-in: an uninvited second HUD element surprises users
		bool logFollowerPerformance{ true };
		float postCombatWindowFadeSeconds{ 5.0f };
		// Opt-in: registering with devbench exposes combat stats on the port
		// the devbench host binds, so it is off unless the user enables it.
		bool enableDevBench{ false };

		// [Debug]
		bool debugLog{ false };
		bool deltaAudit{ false };  // 1 Hz health-delta vs captured-events comparison
	};

	// Single source of truth for the color set: INI load/save and the UI
	// pickers iterate this table, so adding a color touches only Settings.h
	// (plus whatever maps a kind to it in the renderer).
	struct ColorDef
	{
		const char* iniKey;
		const char* uiLabel;
		std::uint32_t Settings::* field;
	};
	inline constexpr std::array<ColorDef, 15> kColorTable{ {
		{ "sPhysical", "Physical", &Settings::colorPhysical },
		{ "sCritical", "Critical", &Settings::colorCritical },
		{ "sBlocked", "Blocked", &Settings::colorBlocked },
		{ "sFire", "Fire", &Settings::colorFire },
		{ "sFrost", "Frost", &Settings::colorFrost },
		{ "sShock", "Shock", &Settings::colorShock },
		{ "sPoison", "Poison", &Settings::colorPoison },
		{ "sMagic", "Magic (untyped)", &Settings::colorMagic },
		{ "sHealing", "Healing", &Settings::colorHealing },
		{ "sMagickaDamage", "Magicka damage", &Settings::colorMagickaDamage },
		{ "sStaminaDamage", "Stamina damage", &Settings::colorStaminaDamage },
		{ "sOriginPlayer", "Marker: your hits", &Settings::colorOriginPlayer },
		{ "sOriginTaken", "Marker: damage you take", &Settings::colorOriginTaken },
		{ "sOriginFollower", "Marker: follower damage", &Settings::colorOriginFollower },
		{ "sOriginNPC", "Marker: NPC fights", &Settings::colorOriginNPC },
	} };
}
