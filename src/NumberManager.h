// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "Capture.h"

namespace FDNG
{
	// One live floating number. Lives in a fixed pool slot; no heap allocation
	// after startup (spec: zero allocations in the combat path).
	struct Number
	{
		bool active{ false };
		RE::FormID victimID{ 0 };
		RE::NiPoint3 anchor;  // world spawn point (spiral offset baked in)
		RE::NiPoint3 arcDir;  // horizontal unit direction for arc/radial motion
		float age{ 0.0f };    // seconds since spawn
		float lifetime{ 1.5f };
		float amount{ 0.0f };
		float mitigated{ 0.0f };
		DamageKind kind{ DamageKind::kPhysical };
		OriginTier origin{ OriginTier::kNPC };
		HitFlags flags;
		char text[24]{};     // "104" / "CRIT 104"
		char subtext[24]{};  // "(-45 resisted)"
	};

	// Per-frame, per-number draw parameters resolved by Resolve().
	struct ResolvedNumber
	{
		const Number* number{ nullptr };
		RE::NiPoint3 worldPos;  // anchor + kinematic offset
		float scale{ 1.0f };
		float alpha{ 1.0f };
	};

	class NumberManager
	{
	public:
		static constexpr std::size_t kPoolCapacity = 128;
		static constexpr std::size_t kQueueCapacity = 64;

		static NumberManager* GetSingleton();

		// Game thread: queue a classified damage event for display.
		void Enqueue(const DamageEvent& a_event);

		// Render thread: drain the queue (merging DoT ticks), advance ages,
		// retire dead numbers, and resolve draw parameters for the live ones.
		// Returns entries in a caller-owned buffer (reused across frames).
		void Update(std::vector<ResolvedNumber>& a_out);

	private:
		void Spawn(const DamageEvent& a_event);
		void BuildText(Number& a_number) const;

		RE::NiPoint3 KinematicOffset(const Number& a_number) const;

		std::array<Number, kPoolCapacity> _pool{};

		std::mutex _queueLock;
		std::array<DamageEvent, kQueueCapacity> _queue{};
		std::size_t _queueCount{ 0 };

		std::chrono::steady_clock::time_point _lastUpdate{};
	};
}
