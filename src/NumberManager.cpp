// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "NumberManager.h"

#include "Settings.h"

namespace FDNG
{
	namespace
	{
		// Kinematic constants, in Skyrim game units (1 unit ≈ 1.428 cm).
		constexpr float kFloatRiseSpeed = 45.0f;
		constexpr float kArcLateralSpeed = 55.0f;
		constexpr float kArcLaunchSpeed = 90.0f;
		constexpr float kArcGravity = 220.0f;
		constexpr float kRadialSpeed = 45.0f;
		constexpr float kSpiralStep = 12.0f;
		constexpr float kFadePortion = 0.3f;  // alpha ramps out over the final 30% of life

		constexpr float kMeterToGameUnit = 1.0f / 0.01428f;
	}

	NumberManager* NumberManager::GetSingleton()
	{
		static NumberManager singleton;
		return &singleton;
	}

	void NumberManager::Enqueue(const DamageEvent& a_event)
	{
		std::scoped_lock lk{ _queueLock };
		if (_queueCount < kQueueCapacity) {
			_queue[_queueCount++] = a_event;
		}
	}

	void NumberManager::BuildText(Number& a_number) const
	{
		// Zero-amount tagged events (RESISTED) show the tag alone.
		if (a_number.amount < 0.5f && a_number.location[0] != '\0') {
			std::snprintf(a_number.text, sizeof(a_number.text), "%s", a_number.location);
			a_number.subtext[0] = '\0';
			return;
		}

		const auto rounded = std::max(1, static_cast<int>(std::lround(a_number.amount)));
		const char* prefix = "";
		char locPrefix[20]{};
		if (a_number.kind == DamageKind::kHealing) {
			prefix = "+";
		} else if (a_number.location[0] != '\0') {
			std::snprintf(locPrefix, sizeof(locPrefix), "%s ", a_number.location);
			prefix = locPrefix;
		} else if (a_number.flags.critical) {
			prefix = "CRIT ";
		} else if (a_number.flags.sneak) {
			prefix = "SNEAK ";
		} else if (a_number.flags.blocked) {
			prefix = "BLOCK ";
		}
		std::snprintf(a_number.text, sizeof(a_number.text), "%s%d", prefix, rounded);

		a_number.subtext[0] = '\0';
		const auto settings = Settings::GetSingleton();
		const bool showMit = settings->showMitigation && a_number.mitigated >= 1.0f;
		const bool showAmp = a_number.ampMult > 0.0f;
		if (showMit && showAmp) {
			std::snprintf(a_number.subtext, sizeof(a_number.subtext), "(-%d resisted) x%.1f",
				static_cast<int>(std::lround(a_number.mitigated)), a_number.ampMult);
		} else if (showMit) {
			std::snprintf(a_number.subtext, sizeof(a_number.subtext), "(-%d resisted)",
				static_cast<int>(std::lround(a_number.mitigated)));
		} else if (showAmp) {
			std::snprintf(a_number.subtext, sizeof(a_number.subtext), "x%.1f", a_number.ampMult);
		}
	}

	void NumberManager::Spawn(const DamageEvent& a_event)
	{
		const auto settings = Settings::GetSingleton();

		// DoT/rapid-tick accumulation: merge into a young, plain number for the
		// same victim and damage type instead of stacking new quads.
		std::size_t victimCount = 0;
		Number* freeSlot = nullptr;
		Number* oldest = nullptr;
		std::size_t activeCount = 0;
		for (auto& n : _pool) {
			if (!n.active) {
				if (!freeSlot) {
					freeSlot = &n;
				}
				continue;
			}
			++activeCount;
			if (!oldest || n.age > oldest->age) {
				oldest = &n;
			}
			if (n.victimID == a_event.victimID) {
				++victimCount;
				// Heals stream in threshold-sized chunks, so give them a wider
				// merge window than damage ticks get.
				const float mergeWindow = a_event.kind == DamageKind::kHealing ?
				                              std::max(settings->dotAccumulationWindow, 0.8f) :
				                              settings->dotAccumulationWindow;
				// Crits and locational hits stand alone; only plain numbers merge.
				if (n.kind == a_event.kind && !n.flags.critical && !a_event.flags.critical &&
					n.location[0] == '\0' && a_event.location[0] == '\0' &&
					n.age < mergeWindow) {
					n.amount += a_event.amount;
					n.mitigated += a_event.mitigated;
					n.age = 0.0f;  // restart the merge window and the animation
					BuildText(n);
					return;
				}
			}
		}

		// Enforce the configured concurrency cap by recycling the oldest number.
		const auto maxQuads = static_cast<std::size_t>(std::clamp(settings->maxConcurrentQuads, 1, static_cast<int>(kPoolCapacity)));
		if (!freeSlot || activeCount >= maxQuads) {
			if (!oldest) {
				return;
			}
			freeSlot = oldest;
		}

		Number& n = *freeSlot;
		n = Number{};
		n.active = true;
		n.victimID = a_event.victimID;
		n.anchor = a_event.anchor;
		n.amount = a_event.amount;
		n.mitigated = a_event.mitigated;
		n.ampMult = a_event.ampMult;
		std::memcpy(n.location, a_event.location, sizeof(n.location));
		n.kind = a_event.kind;
		n.origin = a_event.origin;
		n.flags = a_event.flags;
		n.lifetime = settings->quadLifetimeSeconds * (a_event.flags.critical ? 1.35f : 1.0f);

		// Spiral anti-stacking: successive numbers on the same target step
		// outward on a golden-angle spiral so rapid hits don't overlap.
		const float angle = static_cast<float>(victimCount) * 2.399963f;
		const float radius = kSpiralStep * static_cast<float>(victimCount);
		n.anchor.x += radius * std::cos(angle);
		n.anchor.y += radius * std::sin(angle);
		n.arcDir = { std::cos(angle), std::sin(angle), 0.0f };

		BuildText(n);
	}

	RE::NiPoint3 NumberManager::KinematicOffset(const Number& a_number) const
	{
		const auto settings = Settings::GetSingleton();
		const float t = a_number.age * settings->globalSpeedMultiplier;

		switch (settings->profile) {
		case KinematicProfile::kArc:
			return {
				a_number.arcDir.x * kArcLateralSpeed * t,
				a_number.arcDir.y * kArcLateralSpeed * t,
				kArcLaunchSpeed * t - 0.5f * kArcGravity * t * t
			};
		case KinematicProfile::kRadial:
			{
				// Outward burst that decelerates smoothly (AoE-friendly).
				const float travel = kRadialSpeed * (1.0f - std::exp(-2.5f * t)) / 2.5f * 3.0f;
				return { a_number.arcDir.x * travel, a_number.arcDir.y * travel, 10.0f * t };
			}
		case KinematicProfile::kFloat:
		default:
			return { 0.0f, 0.0f, kFloatRiseSpeed * t };
		}
	}

	void NumberManager::Update(std::vector<ResolvedNumber>& a_out)
	{
		a_out.clear();

		// Frame delta from a real clock: the HUD context's io.DeltaTime is a
		// fixed placeholder, and animation must be frame-rate independent.
		const auto now = std::chrono::steady_clock::now();
		float dt = 0.0f;
		if (_lastUpdate.time_since_epoch().count() != 0) {
			dt = std::clamp(std::chrono::duration<float>(now - _lastUpdate).count(), 0.0f, 0.1f);
		}
		_lastUpdate = now;

		// Freeze aging while the game is paused, or a potion drunk in the
		// inventory expires its number behind the menu.
		if (const auto ui = RE::UI::GetSingleton(); ui && ui->GameIsPaused()) {
			dt = 0.0f;
		}

		{
			std::scoped_lock lk{ _queueLock };
			for (std::size_t i = 0; i < _queueCount; ++i) {
				Spawn(_queue[i]);
			}
			_queueCount = 0;
		}

		const auto settings = Settings::GetSingleton();
		const auto player = RE::PlayerCharacter::GetSingleton();
		const auto playerPos = player ? player->GetPosition() : RE::NiPoint3{};
		const auto maxRadiusSq = [&] {
			const float r = settings->maxVisibilityRadiusMeters * kMeterToGameUnit;
			return r * r;
		}();

		for (auto& n : _pool) {
			if (!n.active) {
				continue;
			}
			n.age += dt;
			if (n.age >= n.lifetime) {
				n.active = false;
				continue;
			}

			// Crowd attenuation (spec §3): scale/alpha tiers by who dealt the
			// damage, with NPC-on-NPC culled entirely beyond the radius.
			float scaleMult = 1.0f;
			float alphaMult = 1.0f;
			switch (n.origin) {
			case OriginTier::kFollower:
				scaleMult = 0.70f;
				alphaMult = 0.60f;
				break;
			case OriginTier::kNPC:
				scaleMult = 0.45f;
				alphaMult = 0.35f;
				if (playerPos.GetSquaredDistance(n.anchor) > maxRadiusSq) {
					continue;
				}
				break;
			default:
				break;
			}

			if (n.flags.critical) {
				scaleMult *= 1.5f;
			}

			// Pop-in over the first 120 ms, fade-out over the last 30% of life.
			const float tn = n.age / n.lifetime;
			scaleMult *= 0.6f + 0.4f * std::min(n.age / 0.12f, 1.0f);
			const float fade = (1.0f - tn) / kFadePortion;
			alphaMult *= std::clamp(fade, 0.0f, 1.0f);

			// Spec §2 sizing curve: FinalScale = clamp(Base + log10(dmg)·Mod, min, ceiling)
			// — big hits read bigger, chip damage stays small.
			const float magnitudeScale = std::clamp(
				settings->baseFontScale + std::log10(std::max(n.amount, 1.0f)) * settings->logScaleModifier,
				0.6f, settings->maxFontScaleCeiling);

			ResolvedNumber resolved;
			resolved.number = &n;
			resolved.worldPos = n.anchor + KinematicOffset(n);
			resolved.scale = scaleMult * magnitudeScale;
			resolved.alpha = alphaMult;
			a_out.push_back(resolved);
		}
	}
}
