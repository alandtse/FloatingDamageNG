// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

namespace FDNG
{
	enum class DamageKind : std::uint8_t
	{
		kPhysical = 0,
		kFire,
		kFrost,
		kShock,
		kPoison,
		kMagic,  // untyped/other magic
		kHealing,
		kMagickaDrain,  // hostile magicka damage (shock secondary, absorb/drain)
		kStaminaDrain   // hostile stamina damage (frost secondary, absorb/drain)
	};

	enum class OriginTier : std::uint8_t
	{
		kPlayer = 0,    // player dealt it
		kFollower,      // a teammate dealt it
		kNPC,           // NPC-on-NPC
		kPlayerVictim,  // player received it
	};

	struct HitFlags
	{
		bool critical{ false };
		bool blocked{ false };
		bool sneak{ false };
		bool powerAttack{ false };
	};

	// One classified damage application, ready to display.
	struct DamageEvent
	{
		RE::FormID victimID{ 0 };
		RE::NiPoint3 anchor;  // world-space spawn point (victim head)
		float amount{ 0.0f };
		float mitigated{ 0.0f };  // resisted/blocked portion, for the subtext
		DamageKind kind{ DamageKind::kPhysical };
		OriginTier origin{ OriginTier::kNPC };
		HitFlags flags;
	};

	// Damage-capture front end: receives raw engine callbacks (from Hooks) and
	// engine events, correlates hit metadata with damage amounts, classifies,
	// and forwards finished DamageEvents to the NumberManager.
	class Capture :
		public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
	{
	public:
		static Capture* GetSingleton();

		// Register the magic-effect sink (call at kDataLoaded).
		void Register();

		// From the HandleHealthDamage vfunc hook (game thread). a_damage is the
		// engine's signed delta (negative for damage).
		void OnHealthDamage(RE::Actor* a_victim, RE::Actor* a_attacker, float a_damage);

		// From the ModActorValue vfunc hook (game thread): a positive kDamage
		// delta on Health is a heal (RestoreActorValue routes through here).
		// Ticks accumulate per actor until they clear the regen-noise threshold.
		void OnHealthRestore(RE::Actor* a_target, float a_amount);

		// From the ModActorValue vfunc hook (game thread): a negative kDamage
		// delta on Health that did NOT come through HandleHealthDamage — magic,
		// enchantments, DoT ticks, script damage. Classified via the recent
		// apply events, falling back to a VR-safe walk of the victim's active
		// effects (MagicTarget::VisitEffects — never GetActiveEffectList, which
		// is a thread-local shim on VR and unsafe under contention).
		void OnMagicDamage(RE::Actor* a_victim, float a_amount);

		// Negative kDamage delta on Magicka/Stamina. Displayed only when it
		// attributes to a hostile effect — self costs (casting, sprinting,
		// power attacks) ride the same path and must stay silent.
		void OnResourceDamage(RE::Actor* a_victim, RE::ActorValue a_value, float a_amount);

		// From the ValueModifierEffect::ModifyActorValue vfunc hook (game
		// thread): THE application point for magic/enchant/potion effect
		// deltas — the engine writes AV modifiers here without dispatching
		// ActorValueOwner::ModActorValue, which is why the AV hook alone
		// missed NPC magic damage. The effect instance provides exact
		// classification (MGEF) and attribution (caster).
		void OnEffectModify(RE::ValueModifierEffect* a_effect, RE::Actor* a_target, float a_value, RE::ActorValue a_actorValue);

		// Debug audit (bDeltaAudit): 1 Hz comparison of each tracked actor's
		// observed health delta vs the captured event stream; logs suspected
		// missed damage. Called from the render tick.
		void AuditTick();

		// From the hit-dispatch thunk (game thread): stash per-victim hit
		// metadata so the matching OnHealthDamage can consume it.
		void OnHitData(const RE::HitData& a_hitData);

		RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override;

	private:
		using Clock = std::chrono::steady_clock;

		struct PendingHit
		{
			Clock::time_point stamp;
			float physicalDamage{ 0.0f };
			float mitigated{ 0.0f };
			HitFlags flags;
		};

		struct RecentMagic
		{
			Clock::time_point stamp;
			DamageKind kind{ DamageKind::kMagic };
			RE::FormID casterID{ 0 };
		};

		struct HealAccum
		{
			Clock::time_point windowStart;
			float amount{ 0.0f };
		};

		// Sub-threshold tick pool: concentration spells apply health damage in
		// sub-point per-frame deltas; pool them until they are worth a number.
		struct TickAccum
		{
			Clock::time_point windowStart;
			float amount{ 0.0f };
		};

		static DamageKind ClassifyMagicKind(const RE::EffectSetting* a_mgef);

		// Shared tail: analytics, display filters, spawn.
		void EmitDamage(RE::Actor* a_victim, RE::Actor* a_attacker, float a_amount, DamageKind a_kind, const HitFlags& a_flags, float a_mitigated);

		// EmitDamage with sub-threshold tick pooling (concentration spells
		// apply in sub-point per-frame deltas).
		void EmitPooledDamage(RE::Actor* a_victim, RE::Actor* a_attacker, float a_amount, DamageKind a_kind);

		// Correlation windows: a weapon hit's HandleHealthDamage lands the same
		// frame; magic apply can precede its first damage tick by a bit longer.
		static constexpr auto kHitWindow = std::chrono::milliseconds(200);
		static constexpr auto kMagicWindow = std::chrono::milliseconds(1500);
		// Heal ticks that don't reach the display threshold within this window
		// are natural regen trickle — discard them.
		static constexpr auto kHealWindow = std::chrono::milliseconds(1500);

		// Try to attribute a resource-damaging effect on the victim via the
		// VR-safe visitor. Matches detrimental effects whose primary or
		// secondary AV is a_value. Returns false when nothing matches.
		static bool FindHostileEffect(RE::Actor* a_victim, RE::ActorValue a_value, DamageKind& a_kindOut, RE::Actor*& a_attackerOut);

		void AuditRecord(RE::FormID a_victimID, float a_delta);  // negative = damage

		std::mutex _lock;
		std::unordered_map<RE::FormID, PendingHit> _pendingHits;
		std::unordered_map<RE::FormID, RecentMagic> _recentMagic;
		std::unordered_map<RE::FormID, HealAccum> _healAccums;
		std::unordered_map<std::uint64_t, TickAccum> _tickAccums;  // key: victimID | kind<<32

		// Audit state (bDeltaAudit only)
		struct AuditEntry
		{
			float lastHealth{ -1.0f };
			float capturedNet{ 0.0f };  // sum of captured deltas since last tick
		};
		std::unordered_map<RE::FormID, AuditEntry> _audit;
		Clock::time_point _lastAudit{};
	};
}
