// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "NumberManager.h"

#include "Settings.h"

namespace FDNG
{
	namespace
	{
		// In Skyrim game units (1 unit ≈ 1.428 cm). Motion speeds/accels now
		// live in the data-driven MotionProfile (Settings).
		constexpr float kSpreadStep = 30.0f;  // diagonal-alternate lateral gap, game units
		constexpr float kFadePortion = 0.3f;  // alpha ramps out over the final 30% of life

		constexpr float kMeterToGameUnit = 1.0f / 0.01428f;
		constexpr float kDegToRad = 0.01745329f;

		// Crowd hierarchy comes from size and outline color (see the
		// renderer), never opacity — alpha attenuation proved to scale
		// straight to invisible in an HMD.
		constexpr float kFollowerScale = 0.85f;
		constexpr float kNPCScale = 0.70f;
		constexpr float kCritScaleBoost = 1.5f;
		constexpr float kPopInSeconds = 0.12f;
		constexpr float kMinMagnitudeScale = 0.6f;
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
		} else if (a_number.flags.bash) {
			prefix = "BASH ";
		} else if (a_number.flags.blocked) {
			prefix = "BLOCK ";
		}
		std::snprintf(a_number.text, sizeof(a_number.text), "%s%d", prefix, rounded);

		a_number.subtext[0] = '\0';
		const auto settings = Settings::GetSingleton();
		const bool showMit = settings->showMitigation && a_number.mitigated >= 1.0f;
		const bool showAmp = a_number.ampMult > 0.0f;
		const char* mitWord = a_number.mitLabel == MitigationLabel::kBlocked ? "blocked" :
		                      a_number.mitLabel == MitigationLabel::kArmor   ? "armor" :
		                                                                       "resisted";
		if (showMit && showAmp) {
			std::snprintf(a_number.subtext, sizeof(a_number.subtext), "(-%d %s) x%.1f",
				static_cast<int>(std::lround(a_number.mitigated)), mitWord, a_number.ampMult);
		} else if (showMit) {
			std::snprintf(a_number.subtext, sizeof(a_number.subtext), "(-%d %s)",
				static_cast<int>(std::lround(a_number.mitigated)), mitWord);
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
				// Crits and locational hits stand alone; only plain numbers of
				// the same origin merge — without the origin check, an NPC
				// hitting the player's target folded into the player's number.
				if (n.kind == a_event.kind && n.origin == a_event.origin &&
					!n.flags.critical && !a_event.flags.critical &&
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
		n.mitLabel = a_event.mitLabel;
		n.kind = a_event.kind;
		n.origin = a_event.origin;
		n.flags = a_event.flags;
		n.lifetime = settings->quadLifetimeSeconds * (a_event.flags.critical ? 1.35f : 1.0f);

		// Screen-relative basis at the target: `right` is horizontal-perp to
		// the player's view, `up` is world Z. Rapid hits de-overlap and burst
		// patterns fan out within this basis.
		RE::NiPoint3 right{ 1.0f, 0.0f, 0.0f };
		if (const auto player = RE::PlayerCharacter::GetSingleton()) {
			auto toVictim = a_event.anchor - player->GetPosition();
			toVictim.z = 0.0f;
			if (const float len = toVictim.Length(); len > 1.0f) {
				right = { toVictim.y / len, -toVictim.x / len, 0.0f };
			}
		}
		const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };
		const auto k = static_cast<int>(victimCount);

		switch (settings->spreadPattern) {
		case SpreadPattern::kRotate:
			{
				// Fireworks: each hit rotates the launch around the view axis; the
				// launch tilts up-and-out so numbers spray diagonally.
				const float ang = static_cast<float>(k) * settings->spawnAngleDeg * kDegToRad;
				const RE::NiPoint3 dir = right * std::cos(ang) + up * std::sin(ang);
				n.launchDir = dir;
				n.spread = { 0.0f, 0.0f, 0.0f };
				break;
			}
		case SpreadPattern::kDiagonalAlternate:
			{
				// Alternate up-left / up-right diagonals at the configured tilt.
				const float side = (k % 2 == 1) ? 1.0f : -1.0f;
				const float tilt = std::clamp(settings->spawnAngleDeg, 0.0f, 89.0f) * kDegToRad;
				n.launchDir = right * (side * std::cos(tilt)) + up * std::sin(tilt);
				n.spread = right * (side * kSpreadStep * 0.5f * static_cast<float>((k + 1) / 2));
				break;
			}
		case SpreadPattern::kAlternate:
		default:
			{
				// Left/right along screen-horizontal with a growing step; a bias
				// shifts the whole distribution toward one side.
				const float side = (k % 2 == 1) ? 1.0f : -1.0f;
				const float biased = std::clamp(side + settings->rapidHitBias, -1.0f, 1.0f);
				const float step = k == 0 ? 0.0f : settings->rapidHitSpread * static_cast<float>((k + 1) / 2);
				n.spread = right * (biased * step);
				n.launchDir = k == 0 ? right : right * (biased >= 0.0f ? 1.0f : -1.0f);
				break;
			}
		}

		BuildText(n);
	}

	RE::NiPoint3 NumberManager::KinematicOffset(const Number& a_number) const
	{
		// Data-driven path: travel along the number's launch direction plus an
		// independent vertical term. Every built-in effect (Float/Arc/Radial/
		// Fireworks) is a parameter set fed through this one integrator.
		const auto& m = Settings::GetSingleton()->motion;
		const float t = a_number.age * Settings::GetSingleton()->globalSpeedMultiplier;

		const float travel = m.lateralDamping > 0.001f ?
		                         m.lateralSpeed * (1.0f - std::exp(-m.lateralDamping * t)) / m.lateralDamping :
		                         m.lateralSpeed * t;
		const float vertical = m.riseSpeed * t + 0.5f * m.riseAccel * t * t;
		return a_number.launchDir * travel + RE::NiPoint3{ 0.0f, 0.0f, vertical };
	}

	void NumberManager::PreviewTick()
	{
		const auto settings = Settings::GetSingleton();
		if (!settings->previewMode) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now - _lastPreview < std::chrono::milliseconds(700)) {
			return;
		}
		_lastPreview = now;

		// Target the console selection (prid) or fall back to the player.
		RE::TESObjectREFR* target = nullptr;
		if (const auto sel = RE::Console::GetSelectedRef()) {
			target = sel.get();
		}
		if (!target) {
			target = RE::PlayerCharacter::GetSingleton();
		}
		const auto actor = target ? target->As<RE::Actor>() : nullptr;
		if (!actor || !actor->Is3DLoaded()) {
			return;
		}

		// Rotate through a representative set so colors, crit sizing, healing,
		// and the origin markers all show while tuning.
		struct Sample
		{
			float amount;
			DamageKind kind;
			bool crit;
			OriginTier origin;
			const char* location;
		};
		static constexpr Sample kSamples[] = {
			{ 42.0f, DamageKind::kPhysical, false, OriginTier::kPlayer, "" },
			{ 88.0f, DamageKind::kFire, true, OriginTier::kPlayer, "" },
			{ 25.0f, DamageKind::kFrost, false, OriginTier::kPlayerVictim, "" },
			{ 60.0f, DamageKind::kShock, false, OriginTier::kPlayer, "HEADSHOT" },
			{ 30.0f, DamageKind::kHealing, false, OriginTier::kPlayer, "" },
		};
		static std::size_t idx = 0;
		const auto& s = kSamples[idx % std::size(kSamples)];
		++idx;

		DamageEvent event;
		event.victimID = actor->GetFormID();
		if (const auto middle = actor->GetMiddleHighProcess(); middle && middle->headNode) {
			event.anchor = middle->headNode->world.translate;
		} else {
			event.anchor = actor->GetPosition();
			event.anchor.z += actor->GetHeight();
		}
		event.amount = s.amount;
		event.kind = s.kind;
		event.origin = s.origin;
		event.flags.critical = s.crit;
		std::snprintf(event.location, sizeof(event.location), "%s", s.location);
		Enqueue(event);
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
		std::size_t radiusCulled = 0;

		for (auto& n : _pool) {
			if (!n.active) {
				continue;
			}
			n.age += dt;
			if (n.age >= n.lifetime) {
				n.active = false;
				continue;
			}

			// Follow the victim while they're loaded (moving enemies, the
			// player mid-heal); keep the last anchor when they unload.
			if (const auto victim = RE::TESForm::LookupByID<RE::Actor>(n.victimID); victim && victim->Is3DLoaded()) {
				if (const auto middle = victim->GetMiddleHighProcess(); middle && middle->headNode) {
					n.anchor = middle->headNode->world.translate;
				}
			}

			// Crowd attenuation: scale/alpha tiers by who dealt the
			// damage, with NPC-on-NPC culled entirely beyond the radius.
			float scaleMult = 1.0f;
			float alphaMult = 1.0f;
			switch (n.origin) {
			case OriginTier::kFollower:
				scaleMult = kFollowerScale;
				break;
			case OriginTier::kNPC:
				scaleMult = kNPCScale;
				if (playerPos.GetSquaredDistance(n.anchor) > maxRadiusSq) {
					++radiusCulled;
					continue;
				}
				break;
			default:
				break;
			}

			if (n.flags.critical) {
				scaleMult *= kCritScaleBoost;
			}

			// Pop-in at spawn, fade-out over the final portion of life.
			const float tn = n.age / n.lifetime;
			scaleMult *= 0.6f + 0.4f * std::min(n.age / kPopInSeconds, 1.0f);
			const float fade = (1.0f - tn) / kFadePortion;
			alphaMult *= std::clamp(fade, 0.0f, 1.0f);

			// Sizing curve: FinalScale = clamp(Base + log10(dmg)·Mod, min, ceiling)
			// — big hits read bigger, chip damage stays small.
			const float magnitudeScale = std::clamp(
				settings->baseFontScale + std::log10(std::max(n.amount, 1.0f)) * settings->logScaleModifier,
				kMinMagnitudeScale, settings->maxFontScaleCeiling);

			// User origin offset, in a view-relative basis (up / toward the
			// player / lateral) so the knobs read the same from any angle.
			RE::NiPoint3 originShift{ 0.0f, 0.0f, settings->originOffsetUp };
			if (settings->originOffsetToward != 0.0f || settings->originOffsetSide != 0.0f) {
				auto toPlayer = playerPos - n.anchor;
				toPlayer.z = 0.0f;
				if (const float len = toPlayer.Length(); len > 1.0f) {
					const RE::NiPoint3 fwd{ toPlayer.x / len, toPlayer.y / len, 0.0f };
					const RE::NiPoint3 side{ -fwd.y, fwd.x, 0.0f };
					originShift += fwd * settings->originOffsetToward + side * settings->originOffsetSide;
				}
			}

			ResolvedNumber resolved;
			resolved.number = &n;
			resolved.worldPos = n.anchor + n.spread + originShift + KinematicOffset(n);
			resolved.scale = scaleMult * magnitudeScale;
			resolved.alpha = alphaMult;
			a_out.push_back(resolved);
		}

		if (radiusCulled > 0 && settings->debugLog) {
			static std::chrono::steady_clock::time_point lastTrace;
			if (now - lastTrace > std::chrono::seconds(1)) {
				lastTrace = now;
				logger::debug("Resolve: {} NPC numbers culled by visibility radius ({:.0f} m)",
					radiusCulled, settings->maxVisibilityRadiusMeters);
			}
		}
	}
}
