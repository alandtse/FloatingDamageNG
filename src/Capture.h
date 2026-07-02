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
		float ampMult{ 0.0f };    // implied external multiplier (0 = none)
		char location[16]{};      // locational tag (e.g. "HEADSHOT"), empty = none
		DamageKind kind{ DamageKind::kPhysical };
		OriginTier origin{ OriginTier::kNPC };
		HitFlags flags;
	};

	// Damage-capture front end. THREADING CONTRACT: the engine writes actor
	// values from job threads, script threads, and the main thread, so the
	// hook entry points (OnXxx) only queue POD (form IDs + amounts) — no actor
	// method calls, no map growth beyond a fixed ring. ProcessQueued() drains
	// on the main thread each frame and does everything else there:
	// resolution, classification, filters, analytics, spawning.
	class Capture :
		public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
	{
	public:
		static Capture* GetSingleton();

		// Register the magic-effect sink (call at kDataLoaded).
		void Register();

		// Hook entries — queue-only, any thread. a_damage/a_value keep the
		// engine's sign (negative = damage).
		void OnHealthDamage(RE::Actor* a_victim, RE::Actor* a_attacker, float a_damage);
		void OnHealthRestore(RE::Actor* a_target, float a_amount);
		void OnMagicDamage(RE::Actor* a_victim, float a_amount);
		void OnResourceDamage(RE::Actor* a_victim, RE::ActorValue a_value, float a_amount);
		void OnEffectModify(RE::ValueModifierEffect* a_effect, RE::Actor* a_target, float a_value, RE::ActorValue a_actorValue);

		// From the hit-dispatch thunk: stash per-victim hit metadata so the
		// matching weapon-hit event can consume it (POD only).
		void OnHitData(const RE::HitData& a_hitData);

		// Main thread (render tick): drain and process the raw queue.
		void ProcessQueued();

		// Debug audit (bDeltaAudit): 1 Hz comparison of each tracked actor's
		// observed health delta vs the captured event stream; logs suspected
		// missed damage. Main thread.
		void AuditTick();

		RE::BSEventNotifyControl ProcessEvent(const RE::TESMagicEffectApplyEvent* a_event, RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override;

	private:
		using Clock = std::chrono::steady_clock;

		// Raw hook payload; everything an OnXxx may capture on a foreign thread.
		struct RawEvent
		{
			enum class Source : std::uint8_t
			{
				kWeaponHit,  // HandleHealthDamage (attacker known)
				kAVDelta,    // ActorValueOwner::ModActorValue fallback (scripts, falls, traps)
				kEffect,     // ValueModifierEffect::ModifyActorValue (mgef + caster known)
				kResource,   // magicka/stamina kDamage delta via the AV fallback
			};
			Source source{ Source::kAVDelta };
			RE::ActorValue av{ RE::ActorValue::kHealth };
			RE::FormID victimID{ 0 };
			RE::FormID attackerID{ 0 };
			RE::FormID mgefID{ 0 };
			float amount{ 0.0f };  // signed engine delta (negative = damage)
		};

		struct PendingHit
		{
			Clock::time_point stamp;
			float physicalDamage{ 0.0f };
			float mitigated{ 0.0f };
			float ampMult{ 0.0f };
			char location[16]{};
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
			float mitigated{ 0.0f };
		};

		static DamageKind ClassifyMagicKind(const RE::EffectSetting* a_mgef);

		void QueueRaw(const RawEvent& a_event);

		// Main-thread processing per source.
		void ProcessWeaponHit(const RawEvent& a_raw, RE::Actor* a_victim);
		void ProcessAVDelta(const RawEvent& a_raw, RE::Actor* a_victim);
		void ProcessEffect(const RawEvent& a_raw, RE::Actor* a_victim);
		void ProcessResource(const RawEvent& a_raw, RE::Actor* a_victim);
		void ProcessRestore(RE::Actor* a_target, float a_amount, RE::Actor* a_healer);

		// Shared tail: analytics, display filters, spawn. Main thread.
		void EmitDamage(RE::Actor* a_victim, RE::Actor* a_attacker, float a_amount, DamageKind a_kind, const HitFlags& a_flags, float a_mitigated,
			float a_ampMult = 0.0f, const char* a_location = nullptr);

		// EmitDamage with sub-threshold tick pooling (concentration spells
		// apply in sub-point per-frame deltas).
		void EmitPooledDamage(RE::Actor* a_victim, RE::Actor* a_attacker, float a_amount, DamageKind a_kind, float a_mitigated = 0.0f);

		// Correlation windows: a weapon hit's HandleHealthDamage lands the same
		// frame; magic apply can precede its first damage tick by a bit longer.
		static constexpr auto kHitWindow = std::chrono::milliseconds(200);
		static constexpr auto kMagicWindow = std::chrono::milliseconds(1500);
		// Heal ticks that don't reach the display threshold within this window
		// are natural regen trickle — discard them.
		static constexpr auto kHealWindow = std::chrono::milliseconds(1500);

		void AuditRecord(RE::FormID a_victimID, float a_delta);  // negative = damage

		// Raw ring: written by hooks on any thread, drained on the main thread.
		static constexpr std::size_t kRawCapacity = 256;
		std::mutex _rawLock;
		std::array<RawEvent, kRawCapacity> _raw{};
		std::size_t _rawCount{ 0 };

		std::mutex _lock;  // guards the maps below (event sink writes off-main)
		std::unordered_map<RE::FormID, PendingHit> _pendingHits;
		std::unordered_map<RE::FormID, RecentMagic> _recentMagic;
		std::unordered_map<RE::FormID, HealAccum> _healAccums;
		std::unordered_map<std::uint64_t, TickAccum> _tickAccums;         // key: victimID | kind<<32
		std::unordered_map<RE::FormID, Clock::time_point> _resistStamps;  // RESISTED tag cooldown

		// Audit state (bDeltaAudit only; main thread)
		struct AuditEntry
		{
			float lastHealth{ -1.0f };
			float capturedNet{ 0.0f };  // sum of captured deltas since last tick
		};
		std::unordered_map<RE::FormID, AuditEntry> _audit;
		Clock::time_point _lastAudit{};
	};
}
