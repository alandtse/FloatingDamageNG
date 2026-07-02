// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "CombatLog.h"

#include "DevBench.h"
#include "Settings.h"

#include <fstream>

namespace FDNG
{
	namespace
	{
		std::string CurrentLocationName()
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return "<unknown>";
			}
			if (const auto location = player->GetCurrentLocation()) {
				if (const char* name = location->GetName(); name && name[0]) {
					return name;
				}
			}
			if (const auto cell = player->GetParentCell()) {
				if (const char* name = cell->GetName(); name && name[0]) {
					return name;
				}
			}
			return "<wilderness>";
		}

		std::filesystem::path LogPath()
		{
			auto dir = SKSE::log::log_directory();
			return dir ? *dir / "FloatingDamageNG-combat.log" : std::filesystem::path{ "FloatingDamageNG-combat.log" };
		}

		std::string WallClockNow()
		{
			const auto now = std::chrono::system_clock::now();
			return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::seconds>(now));
		}
	}

	CombatLog* CombatLog::GetSingleton()
	{
		static CombatLog singleton;
		return &singleton;
	}

	void CombatLog::Register()
	{
		if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESCombatEvent>(this);
			holder->AddEventSink<RE::TESDeathEvent>(this);
			logger::info("Combat analytics sinks registered.");
		}
	}

	float CombatLog::SessionSeconds() const
	{
		const auto end = _sessionActive ? Clock::now() : _sessionEnd;
		return std::chrono::duration<float>(end - _sessionStart).count();
	}

	CombatLog::Combatant& CombatLog::GetCombatant(RE::Actor* a_actor)
	{
		auto& c = _combatants[a_actor->GetFormID()];
		if (c.name.empty()) {
			const char* name = a_actor->GetName();
			c.name = (name && name[0]) ? name : std::format("<{:08X}>", a_actor->GetFormID());
			c.isFollower = a_actor->IsPlayerTeammate();
			c.isHostileToPlayer = a_actor->IsHostileToActor(RE::PlayerCharacter::GetSingleton());
		}
		return c;
	}

	void CombatLog::EnsureSession(RE::Actor* a_hint)
	{
		if (_sessionActive) {
			return;
		}
		const auto player = RE::PlayerCharacter::GetSingleton();
		// Spec §5: open on aggro/first blood involving the player's fight.
		if (!player || (!player->IsInCombat() && !(a_hint && a_hint->IsInCombat()))) {
			return;
		}
		_sessionActive = true;
		++_sessionIndex;
		_sessionStart = Clock::now();
		_location = CurrentLocationName();
		_combatants.clear();
		_playerDamage = 0.0f;
		_playerActiveSeconds = 0.0f;
		_lastPlayerHitAt = -1.0f;
		_dpsSamples.clear();
		_lastSampleDamage = 0.0f;
		logger::info("Combat session #{} started @ {}", _sessionIndex, _location);
	}

	void CombatLog::RecordDamage(RE::Actor* a_attacker, RE::Actor* a_victim, float a_amount, DamageKind, const HitFlags& a_flags)
	{
		if (!Settings::GetSingleton()->enableCombatLog || !a_victim) {
			return;
		}
		std::scoped_lock lk{ _lock };
		EnsureSession(a_victim);
		if (!_sessionActive) {
			return;
		}

		const auto now = SessionSeconds();
		auto& victim = GetCombatant(a_victim);
		victim.damageTaken += a_amount;
		if (victim.firstHitTakenAt < 0.0f) {
			victim.firstHitTakenAt = now;
		}

		if (a_attacker) {
			auto& attacker = GetCombatant(a_attacker);
			attacker.damageDealt += a_amount;
			++attacker.hitsDealt;
			if (a_flags.critical) {
				++attacker.critsDealt;
			}

			if (a_attacker == RE::PlayerCharacter::GetSingleton()) {
				_playerDamage += a_amount;
				_playerActiveSeconds += _lastPlayerHitAt < 0.0f ? 1.0f : std::min(now - _lastPlayerHitAt, kActiveGap);
				_lastPlayerHitAt = now;
			}
		}
	}

	void CombatLog::RecordHeal(RE::Actor* a_target, float a_amount)
	{
		if (!Settings::GetSingleton()->enableCombatLog || !a_target) {
			return;
		}
		std::scoped_lock lk{ _lock };
		if (!_sessionActive) {
			return;
		}
		GetCombatant(a_target).healingReceived += a_amount;
	}

	RE::BSEventNotifyControl CombatLog::ProcessEvent(const RE::TESCombatEvent* a_event, RE::BSTEventSource<RE::TESCombatEvent>*)
	{
		if (!Settings::GetSingleton()->enableCombatLog || !a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}
		if (a_event->newState == RE::ACTOR_COMBAT_STATE::kCombat) {
			const auto actor = a_event->actor ? a_event->actor->As<RE::Actor>() : nullptr;
			const auto target = a_event->targetActor ? a_event->targetActor->As<RE::Actor>() : nullptr;
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (actor == player || target == player || (actor && actor->IsInCombat())) {
				std::scoped_lock lk{ _lock };
				EnsureSession(actor);
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl CombatLog::ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*)
	{
		if (!a_event || !a_event->dead || !a_event->actorDying) {
			return RE::BSEventNotifyControl::kContinue;
		}
		std::scoped_lock lk{ _lock };
		if (!_sessionActive) {
			return RE::BSEventNotifyControl::kContinue;
		}
		if (const auto dying = a_event->actorDying->As<RE::Actor>()) {
			if (const auto it = _combatants.find(dying->GetFormID()); it != _combatants.end()) {
				it->second.diedAt = SessionSeconds();
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	void CombatLog::Tick()
	{
		if (!Settings::GetSingleton()->enableCombatLog) {
			return;
		}
		// Cheap 1 Hz poll — combat-state exit has no reliable single event.
		const auto now = Clock::now();
		if (now - _lastTickCheck < std::chrono::seconds(1)) {
			return;
		}
		_lastTickCheck = now;

		std::scoped_lock lk{ _lock };
		if (!_sessionActive) {
			return;
		}

		// 1 Hz DPS timeline sample for the stats page graph.
		if (_dpsSamples.size() < 900) {
			_dpsSamples.push_back(_playerDamage - _lastSampleDamage);
			_lastSampleDamage = _playerDamage;
		}

		const auto player = RE::PlayerCharacter::GetSingleton();
		if (player && !player->IsInCombat()) {
			CloseSession();
		}
	}

	void CombatLog::CloseSession()
	{
		_sessionActive = false;
		_sessionEnd = Clock::now();

		const auto settings = Settings::GetSingleton();
		const float duration = std::max(SessionSeconds(), 0.01f);
		const float realDPS = _playerDamage / duration;
		const float activeDPS = _playerActiveSeconds > 0.0f ? _playerDamage / _playerActiveSeconds : 0.0f;

		logger::info("Combat session #{} ended: {:.1f}s, player dmg {:.0f} (DPS {:.1f} real / {:.1f} active)",
			_sessionIndex, duration, _playerDamage, realDPS, activeDPS);

		SessionSummary summary;
		summary.index = _sessionIndex;
		summary.startedAt = WallClockNow();
		summary.location = _location;
		summary.duration = duration;
		summary.playerDamage = _playerDamage;
		summary.realDPS = realDPS;
		summary.activeDPS = activeDPS;
		summary.dpsSamples = _dpsSamples;

		const auto playerID = RE::PlayerCharacter::GetSingleton() ? RE::PlayerCharacter::GetSingleton()->GetFormID() : 0;
		for (const auto& [id, c] : _combatants) {
			if (id == playerID) {
				continue;
			}
			if (c.isFollower && !settings->logFollowerPerformance) {
				continue;
			}
			std::string fate;
			if (c.diedAt >= 0.0f) {
				fate = c.firstHitTakenAt >= 0.0f ?
				           std::format("died at {:.1f}s (TTD {:.1f}s)", c.diedAt, c.diedAt - c.firstHitTakenAt) :
				           std::format("died at {:.1f}s", c.diedAt);
			} else if (c.isHostileToPlayer && c.damageTaken > 0.0f) {
				fate = "survived/fled";  // spec §5: threat cleared with health > 0
			}
			summary.combatantLines.push_back(std::format("{}{} [{:08X}] — dealt {:.0f} ({} hits, {} crit{}), taken {:.0f}{}{}",
				c.name,
				c.isFollower ? " (follower)" : (c.isHostileToPlayer ? " (hostile)" : ""),
				id,
				c.damageDealt, c.hitsDealt, c.critsDealt, c.critsDealt == 1 ? "" : "s",
				c.damageTaken,
				c.healingReceived > 0.0f ? std::format(", healed +{:.0f}", c.healingReceived) : "",
				fate.empty() ? "" : std::format(" — {}", fate)));
		}

		if (settings->writeLogToDisk) {
			std::ofstream out(LogPath(), std::ios::app);
			if (out) {
				out << std::format("=== Session #{} — {} @ {} — {:.1f}s ===\n", summary.index, summary.startedAt, summary.location, summary.duration);
				out << std::format("  Player: {:.0f} dmg | DPS {:.1f} real / {:.1f} active ({:.1f}s active)\n",
					summary.playerDamage, summary.realDPS, summary.activeDPS, _playerActiveSeconds);
				for (const auto& line : summary.combatantLines) {
					out << "  " << line << '\n';
				}
				out << '\n';
			} else {
				logger::warn("Could not open combat log file for writing.");
			}
		}

		DevBench::NotifySessionEnded(summary);

		_history.push_back(std::move(summary));
		while (_history.size() > kHistoryCapacity) {
			_history.pop_front();
		}
	}

	std::vector<CombatLog::SessionSummary> CombatLog::GetHistory()
	{
		std::scoped_lock lk{ _lock };
		return { _history.begin(), _history.end() };
	}

	CombatLog::LiveStats CombatLog::GetLiveStats()
	{
		std::scoped_lock lk{ _lock };
		LiveStats stats;
		stats.active = _sessionActive;
		if (_sessionIndex == 0) {
			stats.secondsSinceEnd = FLT_MAX;
			return stats;
		}
		stats.sessionSeconds = SessionSeconds();
		stats.secondsSinceEnd = _sessionActive ? 0.0f : std::chrono::duration<float>(Clock::now() - _sessionEnd).count();
		stats.playerDamage = _playerDamage;
		const float duration = std::max(stats.sessionSeconds, 0.01f);
		stats.realDPS = _playerDamage / duration;
		stats.activeDPS = _playerActiveSeconds > 0.0f ? _playerDamage / _playerActiveSeconds : 0.0f;
		return stats;
	}
}
