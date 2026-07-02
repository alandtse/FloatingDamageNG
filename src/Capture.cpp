// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "Capture.h"

#include "CombatLog.h"
#include "NumberManager.h"
#include "Settings.h"

namespace FDNG
{
	namespace
	{
		RE::NiPoint3 GetAnchorPos(RE::Actor* a_actor)
		{
			if (const auto middle = a_actor->GetMiddleHighProcess(); middle && middle->headNode) {
				return middle->headNode->world.translate;
			}
			auto pos = a_actor->GetPosition();
			pos.z += a_actor->GetHeight();
			return pos;
		}

		OriginTier ClassifyOrigin(RE::Actor* a_victim, RE::Actor* a_attacker)
		{
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (a_victim == player) {
				return OriginTier::kPlayerVictim;
			}
			if (a_attacker == player) {
				return OriginTier::kPlayer;
			}
			if (a_attacker && a_attacker->IsPlayerTeammate()) {
				return OriginTier::kFollower;
			}
			return OriginTier::kNPC;
		}
	}

	Capture* Capture::GetSingleton()
	{
		static Capture singleton;
		return &singleton;
	}

	void Capture::Register()
	{
		if (const auto holder = RE::ScriptEventSourceHolder::GetSingleton()) {
			holder->AddEventSink<RE::TESMagicEffectApplyEvent>(this);
			logger::info("Registered TESMagicEffectApplyEvent sink.");
		}
	}

	DamageKind Capture::ClassifyMagicKind(const RE::EffectSetting* a_mgef)
	{
		if (!a_mgef) {
			return DamageKind::kMagic;
		}
		switch (a_mgef->data.resistVariable) {
		case RE::ActorValue::kResistFire:
			return DamageKind::kFire;
		case RE::ActorValue::kResistFrost:
			return DamageKind::kFrost;
		case RE::ActorValue::kResistShock:
			return DamageKind::kShock;
		case RE::ActorValue::kPoisonResist:
			return DamageKind::kPoison;
		default:
			break;
		}
		if (a_mgef->data.archetype == RE::EffectSetting::Archetype::kValueModifier &&
			a_mgef->data.primaryAV == RE::ActorValue::kHealth &&
			!a_mgef->IsDetrimental()) {
			return DamageKind::kHealing;
		}
		return DamageKind::kMagic;
	}

	RE::BSEventNotifyControl Capture::ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*)
	{
		if (!a_event || !a_event->target) {
			return RE::BSEventNotifyControl::kContinue;
		}
		const auto mgef = RE::TESForm::LookupByID<RE::EffectSetting>(a_event->magicEffect);
		if (!mgef || !(mgef->IsHostile() || mgef->IsDetrimental())) {
			return RE::BSEventNotifyControl::kContinue;
		}
		const auto kind = ClassifyMagicKind(mgef);
		if (kind == DamageKind::kMagic && mgef->data.primaryAV != RE::ActorValue::kHealth) {
			// Untyped and not health-targeting — not useful for attribution.
			return RE::BSEventNotifyControl::kContinue;
		}

		const auto casterRef = a_event->caster.get();
		const auto caster = casterRef ? casterRef->As<RE::Actor>() : nullptr;

		std::scoped_lock lk{ _lock };
		_recentMagic[a_event->target->GetFormID()] =
			RecentMagic{ Clock::now(), kind, caster ? caster->GetFormID() : 0 };
		return RE::BSEventNotifyControl::kContinue;
	}

	void Capture::OnHitData(const RE::HitData& a_hitData)
	{
		const auto target = a_hitData.target.get();
		if (!target) {
			return;
		}

		PendingHit pending;
		pending.stamp = Clock::now();
		pending.physicalDamage = a_hitData.physicalDamage;
		pending.mitigated = std::max(0.0f, a_hitData.resistedPhysicalDamage) + std::max(0.0f, a_hitData.resistedTypedDamage);
		pending.flags.critical = a_hitData.flags.any(RE::HitData::Flag::kCritical);
		pending.flags.blocked = a_hitData.flags.any(RE::HitData::Flag::kBlocked);
		pending.flags.sneak = a_hitData.flags.any(RE::HitData::Flag::kSneakAttack);
		pending.flags.powerAttack = a_hitData.flags.any(RE::HitData::Flag::kPowerAttack);

		// Projectile hits carry the struck skeleton node, so hit location is
		// engine data — no locational-damage mod required (and mod-agnostic
		// when one is installed).
		const auto settings = Settings::GetSingleton();
		const auto sourceRef = a_hitData.sourceRef.get();
		if (const auto projectile = sourceRef ? sourceRef->As<RE::Projectile>() : nullptr) {
			if (settings->showHitLocation) {
				auto& impacts = projectile->GetProjectileRuntimeData().impacts;  // BSSimpleList lacks const iteration
				if (!impacts.empty()) {
					if (const auto node = (*impacts.begin())->damageRootNode; node && !node->name.empty()) {
						for (const auto& tag : settings->locationTags) {
							if (std::regex_match(node->name.c_str(), tag.pattern)) {
								std::snprintf(pending.location, sizeof(pending.location), "%s", tag.label.c_str());
								break;
							}
						}
					}
				}
			}

			// Locational mods scale totalDamage but not physicalDamage, so a
			// total exceeding the physical+crit baseline implies an external
			// multiplier. Heuristic — display styling only.
			if (settings->showAmplification) {
				const float critMult = pending.flags.critical ? std::max(a_hitData.criticalDamageMult, 1.0f) : 1.0f;
				const float baseline = a_hitData.physicalDamage * critMult;
				if (baseline > 0.1f) {
					const float amp = a_hitData.totalDamage / baseline;
					if (amp >= settings->amplificationThreshold) {
						pending.ampMult = amp;
					}
				}
			}
		}

		std::scoped_lock lk{ _lock };
		_pendingHits[target->GetFormID()] = pending;
	}

	void Capture::OnHealthDamage(RE::Actor* a_victim, RE::Actor* a_attacker, float a_damage)
	{
		const auto amount = -a_damage;  // engine passes damage as a negative delta
		if (!a_victim || amount < Settings::GetSingleton()->minDamageToShow) {
			return;
		}

		// Weapon-hit path: HandleHealthDamage covers physical blows; magic and
		// DoT ticks arrive via OnMagicDamage instead. The pending HitData
		// contributes crit/block/sneak flags, mitigation, hit location, and
		// the amplification estimate.
		HitFlags flags;
		float mitigated = 0.0f;
		float ampMult = 0.0f;
		char location[16]{};
		{
			std::scoped_lock lk{ _lock };
			if (const auto it = _pendingHits.find(a_victim->GetFormID());
				it != _pendingHits.end() && Clock::now() - it->second.stamp < kHitWindow) {
				flags = it->second.flags;
				mitigated = it->second.mitigated;
				ampMult = it->second.ampMult;
				std::memcpy(location, it->second.location, sizeof(location));
				_pendingHits.erase(it);
			}
		}

		AuditRecord(a_victim->GetFormID(), -amount);
		EmitDamage(a_victim, a_attacker, amount, DamageKind::kPhysical, flags, mitigated, ampMult, location);
	}

	void Capture::OnResourceDamage(RE::Actor* a_victim, RE::ActorValue a_value, float a_amount)
	{
		if (!a_victim || a_amount <= 0.0f) {
			return;
		}

		// Hostile attribution is mandatory: the caster's own spell costs,
		// sprinting, and power attacks arrive as identical deltas.
		DamageKind kind = a_value == RE::ActorValue::kMagicka ? DamageKind::kMagickaDrain : DamageKind::kStaminaDrain;
		RE::Actor* attacker = nullptr;
		bool hostile = false;
		{
			std::scoped_lock lk{ _lock };
			if (const auto it = _recentMagic.find(a_victim->GetFormID());
				it != _recentMagic.end() && Clock::now() - it->second.stamp < kMagicWindow && it->second.casterID != 0) {
				attacker = RE::TESForm::LookupByID<RE::Actor>(it->second.casterID);
				hostile = attacker != nullptr && attacker != a_victim;
			}
		}
		// No effect-list walk here: this hook fires on arbitrary threads (job
		// threads apply spell costs) while other threads mutate the list, and
		// even the visitor crashes under that contention. Effect-sourced
		// drains attribute exactly via OnEffectModify instead.
		if (!hostile) {
			return;
		}

		EmitPooledDamage(a_victim, attacker, a_amount, kind);
	}

	void Capture::AuditRecord(RE::FormID a_victimID, float a_delta)
	{
		if (!Settings::GetSingleton()->deltaAudit) {
			return;
		}
		std::scoped_lock lk{ _lock };
		_audit[a_victimID].capturedNet += a_delta;
	}

	void Capture::AuditTick()
	{
		if (!Settings::GetSingleton()->deltaAudit) {
			return;
		}
		const auto now = Clock::now();
		if (now - _lastAudit < std::chrono::seconds(1)) {
			return;
		}
		_lastAudit = now;

		std::scoped_lock lk{ _lock };
		for (auto it = _audit.begin(); it != _audit.end();) {
			const auto actor = RE::TESForm::LookupByID<RE::Actor>(it->first);
			if (!actor || actor->IsDead()) {
				it = _audit.erase(it);
				continue;
			}
			const float health = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
			auto& entry = it->second;
			if (entry.lastHealth >= 0.0f) {
				// Observed net = captured net + regen (regen >= 0). If the
				// observed change is meaningfully below the captured floor,
				// damage bypassed both capture hooks.
				const float observedNet = health - entry.lastHealth;
				const float floorNet = entry.capturedNet;
				if (observedNet < floorNet - 5.0f) {
					logger::warn("[audit] '{}' [{:08X}]: observed {:+.1f} hp vs captured {:+.1f} — ~{:.1f} damage NOT captured",
						actor->GetName(), it->first, observedNet, floorNet, floorNet - observedNet);
				}
			}
			entry.lastHealth = health;
			entry.capturedNet = 0.0f;
			++it;
		}
	}

	void Capture::OnMagicDamage(RE::Actor* a_victim, float a_amount)
	{
		const auto settings = Settings::GetSingleton();
		if (!a_victim || a_amount <= 0.0f) {
			return;
		}

		// Classify by the most recent hostile apply event. No active-effect
		// walk fallback: this hook fires on arbitrary threads while other
		// threads mutate the effect list (crashes even through the visitor);
		// effect-sourced damage classifies exactly in OnEffectModify anyway,
		// so anything unmatched here is falls/traps/script damage.
		DamageKind kind = DamageKind::kMagic;
		RE::Actor* attacker = nullptr;
		bool classified = false;
		{
			std::scoped_lock lk{ _lock };
			if (const auto it = _recentMagic.find(a_victim->GetFormID());
				it != _recentMagic.end() && Clock::now() - it->second.stamp < kMagicWindow) {
				kind = it->second.kind;
				if (it->second.casterID != 0) {
					attacker = RE::TESForm::LookupByID<RE::Actor>(it->second.casterID);
				}
				classified = true;
			}
		}
		if (!classified) {
			kind = DamageKind::kPhysical;  // falls, traps, script damage
		}

		AuditRecord(a_victim->GetFormID(), -a_amount);
		EmitPooledDamage(a_victim, attacker, a_amount, kind);
	}

	void Capture::OnEffectModify(RE::ValueModifierEffect* a_effect, RE::Actor* a_target, float a_value, RE::ActorValue a_actorValue)
	{
		const auto settings = Settings::GetSingleton();
		if (!a_effect || !a_target || a_value == 0.0f) {
			return;
		}

		const auto mgef = a_effect->GetBaseObject();
		const auto caster = a_effect->caster.get().get();

		if (a_actorValue == RE::ActorValue::kHealth) {
			if (a_value > 0.0f) {
				OnHealthRestore(a_target, a_value);  // has its own gates + pooling + analytics
				return;
			}
			const auto kind = ClassifyMagicKind(mgef);
			AuditRecord(a_target->GetFormID(), a_value);
			EmitPooledDamage(a_target, caster, -a_value, kind == DamageKind::kHealing ? DamageKind::kMagic : kind);
			return;
		}

		if (a_value < 0.0f &&
			((a_actorValue == RE::ActorValue::kMagicka && settings->showMagickaDamage) ||
				(a_actorValue == RE::ActorValue::kStamina && settings->showStaminaDamage))) {
			// Hostile-only: a caster's own costs never come through effects,
			// but beneficial effects with resource costs do — require a
			// detrimental effect from someone else.
			if (!mgef || !(mgef->IsHostile() || mgef->IsDetrimental()) || !caster || caster == a_target) {
				return;
			}
			const auto kind = a_actorValue == RE::ActorValue::kMagicka ? DamageKind::kMagickaDrain : DamageKind::kStaminaDrain;
			EmitPooledDamage(a_target, caster, -a_value, kind);
		}
	}

	void Capture::EmitPooledDamage(RE::Actor* a_victim, RE::Actor* a_attacker, float a_amount, DamageKind a_kind)
	{
		const auto settings = Settings::GetSingleton();

		// Concentration spells tick in sub-point deltas; pool them per
		// victim+type until they clear the display threshold.
		float emit = a_amount;
		if (a_amount < settings->minDamageToShow) {
			const auto now = Clock::now();
			const auto key = static_cast<std::uint64_t>(a_victim->GetFormID()) |
			                 (static_cast<std::uint64_t>(std::to_underlying(a_kind)) << 32);
			std::scoped_lock lk{ _lock };
			auto& accum = _tickAccums[key];
			if (accum.amount == 0.0f || now - accum.windowStart > kMagicWindow) {
				accum.windowStart = now;
				accum.amount = 0.0f;
			}
			accum.amount += a_amount;
			if (accum.amount < settings->minDamageToShow) {
				return;
			}
			emit = accum.amount;
			accum.amount = 0.0f;
			accum.windowStart = now;
		}

		EmitDamage(a_victim, a_attacker, emit, a_kind, HitFlags{}, 0.0f);
	}

	void Capture::EmitDamage(RE::Actor* a_victim, RE::Actor* a_attacker, float a_amount, DamageKind a_kind, const HitFlags& a_flags, float a_mitigated,
		float a_ampMult, const char* a_location)
	{
		const auto settings = Settings::GetSingleton();
		const auto origin = ClassifyOrigin(a_victim, a_attacker);

		// Analytics sees every application, independent of the display filters.
		CombatLog::GetSingleton()->RecordDamage(a_attacker, a_victim, a_amount, a_kind, a_flags);

		DamageEvent event;
		event.victimID = a_victim->GetFormID();
		event.anchor = GetAnchorPos(a_victim);
		event.amount = a_amount;
		event.mitigated = a_mitigated;
		event.ampMult = a_ampMult;
		if (a_location && a_location[0]) {
			std::snprintf(event.location, sizeof(event.location), "%s", a_location);
		}
		event.kind = a_kind;
		event.origin = origin;
		event.flags = a_flags;

		switch (origin) {
		case OriginTier::kPlayer:
			if (!settings->showPlayerDamageDealt) {
				return;
			}
			break;
		case OriginTier::kFollower:
			if (!settings->showFollowerDamageDealt) {
				return;
			}
			break;
		case OriginTier::kNPC:
			if (!settings->showNPCOnNPCDamage) {
				return;
			}
			break;
		case OriginTier::kPlayerVictim:
			// Spawned regardless of camera; the renderer suppresses player
			// numbers while in first person (flat and VR alike), so VR third
			// person still shows them (spec §4's head-locked mode comes later).
			if (!settings->showPlayerDamageTaken) {
				return;
			}
			break;
		}

		if (settings->debugLog) {
			logger::debug("Damage: victim={:08X} amount={:.1f} kind={} origin={} crit={}",
				event.victimID, event.amount, std::to_underlying(event.kind),
				std::to_underlying(event.origin), event.flags.critical);
		}

		NumberManager::GetSingleton()->Enqueue(event);
	}

	void Capture::OnHealthRestore(RE::Actor* a_target, float a_amount)
	{
		const auto settings = Settings::GetSingleton();
		if (!a_target || a_amount <= 0.0f || !settings->showHealing) {
			return;
		}

		const auto origin = ClassifyOrigin(a_target, nullptr);  // healer unknown here; tier by the recipient
		if (origin == OriginTier::kNPC && !settings->showNPCOnNPCDamage) {
			return;
		}

		// Only heals that repair actual damage are visible in-game; a full-health
		// actor's restore calls are engine bookkeeping.
		if (a_target->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth) >= -0.01f) {
			return;
		}

		AuditRecord(a_target->GetFormID(), a_amount);

		// Accumulate ticks: healing effects apply per frame in sub-point deltas.
		// Only a stream that clears the threshold within the window is a real
		// heal; natural regen never gets there and silently expires.
		const auto now = Clock::now();
		float emit = 0.0f;
		{
			std::scoped_lock lk{ _lock };
			auto& accum = _healAccums[a_target->GetFormID()];
			if (accum.amount == 0.0f || now - accum.windowStart > kHealWindow) {
				accum.windowStart = now;
				accum.amount = 0.0f;
			}
			accum.amount += a_amount;
			if (accum.amount >= settings->minHealToShow) {
				emit = accum.amount;
				// Keep the window open so the ongoing stream merges into the
				// same on-screen number via the DoT accumulator.
				accum.amount = 0.0f;
				accum.windowStart = now;
			}
		}
		if (emit <= 0.0f) {
			return;
		}

		CombatLog::GetSingleton()->RecordHeal(a_target, emit);

		DamageEvent event;
		event.victimID = a_target->GetFormID();
		event.anchor = GetAnchorPos(a_target);
		event.amount = emit;
		event.kind = DamageKind::kHealing;
		event.origin = origin;

		if (settings->debugLog) {
			logger::debug("Heal: target={:08X} amount={:.1f} origin={}",
				event.victimID, event.amount, std::to_underlying(event.origin));
		}

		NumberManager::GetSingleton()->Enqueue(event);
	}
}
