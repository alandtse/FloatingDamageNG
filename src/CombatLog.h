// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "Capture.h"

namespace FDNG
{
	// Combat analytics. Sessions open on combat (any
	// participant, not just the player) and close once the player is out of
	// combat AND damage has stopped flowing; per-combatant damage, healing,
	// crits, time-to-die, and fled state accumulate in between. Session
	// reports append to <SKSE logs>/FloatingDamageNG-combat.log and feed the
	// live DPS readout.
	//
	// THREADING: the event sinks run on engine threads and only stash POD;
	// everything else runs on the main thread (RecordDamage/RecordHeal are
	// called from Capture::ProcessQueued, Tick from the render hook). Engine
	// functions are never called while _lock is held — a sink thread waiting
	// on _lock can hold engine locks, and taking them in the opposite order
	// from under _lock deadlocks the game.
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

		// Structured per-combatant result — the stats table renders these
		// directly (never pre-format to strings; the UI can't sort strings).
		struct CombatantSummary
		{
			std::string name;
			bool isFollower{ false };
			bool isHostileToPlayer{ false };
			bool died{ false };
			bool fled{ false };
			float damageDealt{ 0.0f };
			float damageTaken{ 0.0f };
			float healingReceived{ 0.0f };
			int hitsDealt{ 0 };
			int critsDealt{ 0 };
			float timeToDie{ -1.0f };  // first hit taken -> death; -1 when n/a
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
			float peakDPS{ 0.0f };                     // max 1 Hz sample
			std::vector<float> dpsSamples;             // player damage per second, 1 Hz
			std::vector<CombatantSummary> combatants;  // sorted by damage dealt, desc
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

		// Close and persist any open session immediately (game save, load,
		// new game) so data isn't lost when the process goes away before the
		// idle close fires. Main thread only.
		void Flush();

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
		void WriteDiskReport(const SessionSummary& a_summary);

		Combatant& GetCombatant(RE::Actor* a_actor);
		float SessionSeconds() const;

		// Close only after the fight actually stops: the player being out of
		// combat is not enough while NPCs are still trading blows.
		static constexpr auto kIdleClose = std::chrono::seconds(4);

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
		Clock::time_point _lastDamageAt{};

		// Sink→main-thread handoff (POD only; sinks never take _lock).
		std::atomic<RE::FormID> _combatHint{ 0 };
		std::mutex _deathLock;
		std::array<RE::FormID, 32> _deaths{};
		std::size_t _deathCount{ 0 };
	};
}
