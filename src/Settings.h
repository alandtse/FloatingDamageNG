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

	struct Settings
	{
		static Settings* GetSingleton();

		void Load();
		void Save() const;  // write current values back to the INI (used by the SMF config UI)

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

		// [Font]
		std::string fontPath;  // empty = auto (mod override, then Windows bold fonts)

		// [Analytics]
		bool enableCombatLog{ true };      // master switch for the analytics module
		bool writeLogToDisk{ true };       // append session reports to FloatingDamageNG-combat.log
		bool enableLiveDPSWindow{ true };  // small live DPS readout (flat only for now)
		bool logFollowerPerformance{ true };
		float postCombatWindowFadeSeconds{ 5.0f };

		// [Debug]
		bool debugLog{ false };
		bool deltaAudit{ false };  // 1 Hz health-delta vs captured-events comparison
	};
}
