// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "Hooks.h"

#include "Capture.h"
#include "Settings.h"

namespace FDNG::Hooks
{
	namespace
	{
		// Actor::HandleHealthDamage — fires for every health damage application
		// (melee, ranged, magic, DoT ticks, traps, falls) with the true
		// post-mitigation amount (a_damage is negative for damage). It is
		// SKYRIM_REL_VR_VIRTUAL: slot 0x104 on SE/AE, 0x106 on VR. Hooked on
		// both concrete actor vtables; each needs its own original-func slot.
		// Set while the engine is inside HandleHealthDamage on this thread, so
		// the nested ModActorValue write of the same damage isn't double-counted.
		thread_local int t_inHandleHealthDamage = 0;

		// Set while inside ValueModifierEffect::ModifyActorValue — that hook
		// records the delta with full effect attribution, so any nested AV
		// write must not be recorded again by the ModActorValue fallback.
		thread_local int t_inEffectModify = 0;

		template <class TActor>
		struct HandleHealthDamage
		{
			static void thunk(RE::Actor* a_this, RE::Actor* a_attacker, float a_damage)
			{
				++t_inHandleHealthDamage;
				func(a_this, a_attacker, a_damage);
				--t_inHandleHealthDamage;
				Capture::GetSingleton()->OnHealthDamage(a_this, a_attacker, a_damage);
			}
			static inline REL::Relocation<decltype(thunk)> func;

			static void Install()
			{
				const std::size_t idx = REL::Module::IsVR() ? 0x106 : 0x104;
				REL::Relocation<std::uintptr_t> vtbl{ TActor::VTABLE[0] };
				func = vtbl.write_vfunc(idx, thunk);
			}
		};

		// ActorValueOwner::ModActorValue (vfunc 0x06) — RestoreActorValue routes
		// through it, so a positive kDamage delta on Health is a heal (per tick;
		// Capture accumulates and thresholds out regen trickle). ActorValueOwner
		// is VTABLE[5]: TESObjectREFR contributes secondary vtables first
		// (BSHandleRefObject, BSTEventSink<BSAnimationGraphEvent>,
		// IAnimationGraphManagerHolder), then MagicTarget, then ActorValueOwner
		// at offset 0xB0 (SE/AE) / 0xB8 (VR).
		template <class TActor>
		struct ModActorValue
		{
			static void thunk(RE::ActorValueOwner* a_this, RE::ACTOR_VALUE_MODIFIER a_modifier, RE::ActorValue a_value, float a_amount)
			{
				if (a_modifier == RE::ACTOR_VALUE_MODIFIER::kDamage && t_inEffectModify == 0) {
					const auto actor = stl::adjust_pointer<RE::Actor>(a_this, -static_cast<std::ptrdiff_t>(REL::Module::IsVR() ? 0xB8 : 0xB0));
					const auto settings = Settings::GetSingleton();
					if (a_value == RE::ActorValue::kHealth) {
						if (a_amount > 0.0f) {
							Capture::GetSingleton()->OnHealthRestore(actor, a_amount);
						} else if (a_amount < 0.0f && t_inHandleHealthDamage == 0) {
							// Health damage that skipped HandleHealthDamage —
							// the magic / DoT / script path.
							Capture::GetSingleton()->OnMagicDamage(actor, -a_amount);
						}
					} else if (a_amount < 0.0f &&
							   ((a_value == RE::ActorValue::kMagicka && settings->showMagickaDamage) ||
								   (a_value == RE::ActorValue::kStamina && settings->showStaminaDamage))) {
						Capture::GetSingleton()->OnResourceDamage(actor, a_value, -a_amount);
					}
				}
				func(a_this, a_modifier, a_value, a_amount);
			}
			static inline REL::Relocation<decltype(thunk)> func;

			static void Install()
			{
				REL::Relocation<std::uintptr_t> vtbl{ TActor::VTABLE[5] };
				func = vtbl.write_vfunc(0x06, thunk);
			}
		};

		// ValueModifierEffect::ModifyActorValue (vfunc 0x20) — THE application
		// point for magic/enchant/potion deltas. The engine writes the AV
		// modifiers here without dispatching ActorValueOwner::ModActorValue,
		// so the AV hook alone misses effect damage entirely. Hooked on every
		// damage-carrying subclass vtable (each has its own slot).
		template <class TEffect>
		struct ModifyActorValueEffect
		{
			static void thunk(RE::ValueModifierEffect* a_this, RE::Actor* a_actor, float a_value, RE::ActorValue a_actorValue)
			{
				++t_inEffectModify;
				func(a_this, a_actor, a_value, a_actorValue);
				--t_inEffectModify;
				Capture::GetSingleton()->OnEffectModify(a_this, a_actor, a_value, a_actorValue);
			}
			static inline REL::Relocation<decltype(thunk)> func;

			static void Install()
			{
				REL::Relocation<std::uintptr_t> vtbl{ TEffect::VTABLE[0] };
				func = vtbl.write_vfunc(0x20, thunk);
			}
		};

		// The engine's hit-dispatch call site (same one KillFeed hooks): the
		// live HitData carries damage split, mitigation, and crit/block/sneak
		// flags. The callee's signature differs between AE and SE/VR, so two
		// thunks exist and Install() picks by runtime (the NG DLL serves all
		// three from one binary).
		struct SendHitEventSE
		{
			static void thunk(RE::ScriptEventSourceHolder* a_holder,
				RE::NiPointer<RE::TESObjectREFR>& a_target,
				RE::NiPointer<RE::TESObjectREFR>& a_aggressor,
				RE::FormID a_source, RE::FormID a_projectile, RE::HitData& a_hitData)
			{
				Capture::GetSingleton()->OnHitData(a_hitData);
				func(a_holder, a_target, a_aggressor, a_source, a_projectile, a_hitData);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct SendHitEventAE
		{
			static void thunk(RE::AIProcess* a_targetProcess, RE::HitData& a_hitData)
			{
				Capture::GetSingleton()->OnHitData(a_hitData);
				func(a_targetProcess, a_hitData);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void Install()
	{
		HandleHealthDamage<RE::Character>::Install();
		HandleHealthDamage<RE::PlayerCharacter>::Install();

		ModActorValue<RE::Character>::Install();
		ModActorValue<RE::PlayerCharacter>::Install();

		ModifyActorValueEffect<RE::ValueModifierEffect>::Install();
		ModifyActorValueEffect<RE::DualValueModifierEffect>::Install();
		ModifyActorValueEffect<RE::PeakValueModifierEffect>::Install();
		ModifyActorValueEffect<RE::AbsorbEffect>::Install();
		ModifyActorValueEffect<RE::AccumulatingValueModifierEffect>::Install();
		ModifyActorValueEffect<RE::TargetValueModifierEffect>::Install();
		ModifyActorValueEffect<RE::ValueAndConditionsEffect>::Install();

		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(37633, 38586), OFFSET(0x16A, 0xFA) };
		if (REL::Module::IsAE()) {
			stl::write_thunk_call<SendHitEventAE>(target.address());
		} else {
			stl::write_thunk_call<SendHitEventSE>(target.address());
		}

		logger::info("Damage hooks installed (HandleHealthDamage idx {:#x}, hit thunk {}).",
			REL::Module::IsVR() ? 0x106 : 0x104, REL::Module::IsAE() ? "AE" : "SE/VR");
	}
}
