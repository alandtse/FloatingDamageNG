// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "Capture.h"

namespace FDNG
{
	// Combat analytics (spec §5, VR-CALM). Sessions open when the player enters
	// combat and close when the threat register clears; per-combatant damage,
	// healing, crits, time-to-die, and fled state accumulate in between. Session
	// reports append to <SKSE logs>/FloatingDamageNG-combat.log and feed the
	// live DPS readout.
	class CombatLog :
		public RE::BSTEventSink<RE::TESCombatEvent>,
		public RE::BSTEventSink<RE::TESDeathEvent>
	{
	public:
		struct LiveStats
		{
			bool active{ false };
			float sessionSeconds{ 0.0f };
			float secondsSinceEnd{ 0.0f };  // valid when !active; drives the fade-out
			float playerDamage{ 0.0f };
			float realDPS{ 0.0f };
			float activeDPS{ 0.0f };
		};

		// Finished-session record kept for in-game browsing (SMF stats page).
		struct SessionSummary
		{
			int index{ 0 };
			std::string startedAt;  // wall clock
			std::string location;
			float duration{ 0.0f };
			float playerDamage{ 0.0f };
			float realDPS{ 0.0f };
			float activeDPS{ 0.0f };
			std::vector<float> dpsSamples;  // player damage per second, 1 Hz
			std::vector<std::string> combatantLines;
		};

		static CombatLog* GetSingleton();

		// Copy of the recent session history (UI thread safe).
		std::vector<SessionSummary> GetHistory();

		// Register the combat/death sinks (call at kDataLoaded).
		void Register();

		// From Capture (game thread): every classified damage/heal application.
		void RecordDamage(RE::Actor* a_attacker, RE::Actor* a_victim, float a_amount, DamageKind a_kind, const HitFlags& a_flags);
		void RecordHeal(RE::Actor* a_target, float a_amount);

		// Called once per frame from the renderer: closes the session once the
		// player has left combat, and snapshots the live numbers.
		void Tick();

		LiveStats GetLiveStats();

		RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*) override;
		RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override;

	private:
		using Clock = std::chrono::steady_clock;

		struct Combatant
		{
			std::string name;
			bool isFollower{ false };
			bool isHostileToPlayer{ false };
			float damageDealt{ 0.0f };
			float damageTaken{ 0.0f };
			float healingReceived{ 0.0f };
			int hitsDealt{ 0 };
			int critsDealt{ 0 };
			float firstHitTakenAt{ -1.0f };  // session-relative seconds
			float diedAt{ -1.0f };
		};

		void EnsureSession(RE::Actor* a_hint);
		void CloseSession();

		Combatant& GetCombatant(RE::Actor* a_actor);
		float SessionSeconds() const;

		// Active-DPS gap rule (ACT/Details convention): time between player
		// hits counts as "active" only up to this many seconds.
		static constexpr float kActiveGap = 3.0f;

		std::mutex _lock;

		bool _sessionActive{ false };
		int _sessionIndex{ 0 };
		Clock::time_point _sessionStart{};
		Clock::time_point _sessionEnd{};
		std::string _location;
		std::unordered_map<RE::FormID, Combatant> _combatants;

		float _playerDamage{ 0.0f };
		float _playerActiveSeconds{ 0.0f };
		float _lastPlayerHitAt{ -1.0f };

		std::vector<float> _dpsSamples;
		float _lastSampleDamage{ 0.0f };

		static constexpr std::size_t kHistoryCapacity = 20;
		std::deque<SessionSummary> _history;

		Clock::time_point _lastTickCheck{};
	};
}
