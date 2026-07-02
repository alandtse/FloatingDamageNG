// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.
//
// Optional in-game config + combat-stats UI, hosted by SKSE Menu Framework 3.
// SMF resolves everything through GetProcAddress at call time, so this whole
// module is inert when the framework DLL isn't installed. Widgets draw through
// the ImGuiMCP indirection table (the HOST's ImGui context) — never through
// our own statically-linked ImGui.

// This TU is built WITHOUT the project PCH (see xmake.lua): the SMF header's
// cimgui-style typedefs are incompatible with the real imgui.h the PCH pulls
// in, so the includes below reproduce the non-ImGui part of pch.h.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <RE/Skyrim.h>
#include <REX/REX.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace logger = SKSE::log;

#include "SKSEMenuFramework.h"

#include "CombatLog.h"
#include "DevBench.h"
#include "Settings.h"
#include "UI.h"

namespace FDNG::UI
{
	namespace
	{
		void RgbToFloat3(std::uint32_t a_rgb, float a_out[3])
		{
			a_out[0] = static_cast<float>((a_rgb >> 16) & 0xFF) / 255.0f;
			a_out[1] = static_cast<float>((a_rgb >> 8) & 0xFF) / 255.0f;
			a_out[2] = static_cast<float>(a_rgb & 0xFF) / 255.0f;
		}

		std::uint32_t Float3ToRgb(const float a_in[3])
		{
			const auto ch = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
			return (ch(a_in[0]) << 16) | (ch(a_in[1]) << 8) | ch(a_in[2]);
		}

		// devbench writes the port it bound to runtime.json on startup; we read
		// it for display only (the integration itself is in-process). Cached —
		// file I/O every frame would hitch while the panel is open.
		int ReadDevBenchPort()
		{
			static int cached = -1;
			static std::chrono::steady_clock::time_point lastRead;
			const auto now = std::chrono::steady_clock::now();
			if (cached >= 0 && now - lastRead < std::chrono::seconds(2)) {
				return cached;
			}
			lastRead = now;
			cached = 0;
			std::ifstream in("Data/SKSE/Plugins/devbench/runtime.json");
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (const auto pos = text.find("\"port\""); pos != std::string::npos) {
				if (const auto colon = text.find(':', pos); colon != std::string::npos) {
					cached = std::atoi(text.c_str() + colon + 1);
				}
			}
			return cached;
		}

		void ColorRow(const char* a_label, std::uint32_t& a_color)
		{
			float rgb[3];
			RgbToFloat3(a_color, rgb);
			if (ImGuiMCP::ColorEdit3(a_label, rgb, 0)) {
				a_color = Float3ToRgb(rgb);
			}
		}

		// One-line explanation on hover for controls whose label can't carry it.
		void Tip(const char* a_text)
		{
			if (ImGuiMCP::IsItemHovered(0)) {
				ImGuiMCP::SetTooltip("%s", a_text);
			}
		}

		void __stdcall RenderSettings()
		{
			auto* s = Settings::GetSingleton();

			if (ImGuiMCP::CollapsingHeader("What to show", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGuiMCP::Checkbox("Your damage", &s->showPlayerDamageDealt);
				ImGuiMCP::Checkbox("Follower damage", &s->showFollowerDamageDealt);
				ImGuiMCP::Checkbox("NPC-vs-NPC damage", &s->showNPCOnNPCDamage);
				Tip("Fights you aren't part of. Rendered smaller and dimmer, and hidden beyond the visibility radius.");
				ImGuiMCP::Checkbox("Damage you take", &s->showPlayerDamageTaken);
				ImGuiMCP::Checkbox("Player numbers in first person", &s->showFirstPersonNumbers);
				Tip("Pinned to a screen spot on flat, anchored ahead of you in VR. Off = hidden while in first person.");
				ImGuiMCP::Checkbox("Healing", &s->showHealing);
				ImGuiMCP::Checkbox("Magicka damage", &s->showMagickaDamage);
				Tip("Hostile drains only (shock spells, absorb effects). Your own casting costs never show.");
				ImGuiMCP::Checkbox("Stamina damage", &s->showStaminaDamage);
				Tip("Hostile drains only (frost spells, absorb effects). Sprinting and power attacks never show.");
				ImGuiMCP::Checkbox("Mitigation subtext", &s->showMitigation);
				Tip("The small \"(-45 armor)\" / \"(-20 resisted)\" line under a number.");
				ImGuiMCP::Checkbox("Hit location tags", &s->showHitLocation);
				Tip("HEADSHOT etc. on bow/crossbow hits, from where the arrow actually struck.");
				ImGuiMCP::SliderFloat("NPC visibility radius (m)", &s->maxVisibilityRadiusMeters, 5.0f, 100.0f, "%.0f", 0);
				ImGuiMCP::SliderInt("Max numbers on screen", &s->maxConcurrentQuads, 5, 128, "%d", 0);
			}

			if (ImGuiMCP::CollapsingHeader("Size and motion", ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGuiMCP::SliderFloat("Base size", &s->baseFontScale, 0.5f, 2.0f, "%.2f", 0);
				ImGuiMCP::SliderFloat("Big hits grow by", &s->logScaleModifier, 0.0f, 1.0f, "%.2f", 0);
				Tip("How much larger high-damage numbers render (logarithmic in the damage). 0 = all numbers equal size.");
				ImGuiMCP::SliderFloat("Max size multiplier", &s->maxFontScaleCeiling, 1.0f, 3.0f, "%.2f", 0);
				int profile = static_cast<int>(s->profile);
				const char* profiles[] = { "Float (straight rise)", "Arc (tossed sideways)", "Radial burst" };
				if (ImGuiMCP::Combo("Motion", &profile, profiles, 3, -1)) {
					s->profile = static_cast<KinematicProfile>(profile);
				}
				ImGuiMCP::SliderFloat("Speed", &s->globalSpeedMultiplier, 0.25f, 3.0f, "%.2f", 0);
				ImGuiMCP::SliderFloat("Lifetime (s)", &s->quadLifetimeSeconds, 0.5f, 4.0f, "%.2f", 0);
			}

			if (ImGuiMCP::CollapsingHeader("Thresholds", 0)) {
				ImGuiMCP::SliderFloat("Min damage to show", &s->minDamageToShow, 0.0f, 25.0f, "%.1f", 0);
				Tip("Smaller ticks pool up until they cross this, then show as one number.");
				ImGuiMCP::SliderFloat("Min heal to show", &s->minHealToShow, 1.0f, 50.0f, "%.1f", 0);
				Tip("Filters natural health regen; real heals accumulate past it quickly.");
				ImGuiMCP::SliderFloat("Merge window (s)", &s->dotAccumulationWindow, 0.1f, 2.0f, "%.2f", 0);
				Tip("Repeat hits of the same type on one target within this window add into the existing number.");
			}

			if (ImGuiMCP::CollapsingHeader("Colors", 0)) {
				for (const auto& def : kColorTable) {
					ColorRow(def.uiLabel, s->*def.field);
				}
			}

			if (ImGuiMCP::CollapsingHeader("Analytics", 0)) {
				ImGuiMCP::Checkbox("Combat log", &s->enableCombatLog);
				Tip("Track each fight: per-combatant damage, healing, crits, time-to-die, DPS. Feeds the Combat Stats page.");
				ImGuiMCP::BeginDisabled(!s->enableCombatLog);
				ImGuiMCP::Checkbox("Write sessions to disk", &s->writeLogToDisk);
				Tip("Appends session reports to FloatingDamageNG-combat.log next to your SKSE logs (rotated at 5 MB).");
				ImGuiMCP::Checkbox("Live DPS readout", &s->enableLiveDPSWindow);
				Tip("Small top-right overlay during combat. Flat screen only.");
				ImGuiMCP::Checkbox("Include followers in reports", &s->logFollowerPerformance);
				if (ImGuiMCP::Checkbox("devbench integration", &s->enableDevBench) && s->enableDevBench) {
					DevBench::Connect();
				}
				Tip("Registers a floatingdamage.stats tool with the devbench dev harness, exposing stats on its local port. For tooling; leave off otherwise.");
				if (s->enableDevBench) {
					if (DevBench::IsConnected()) {
						if (const int port = ReadDevBenchPort(); port > 0) {
							ImGuiMCP::Text("devbench host present (build %u), bound on 127.0.0.1:%d", DevBench::HostBuild(), port);
						} else {
							ImGuiMCP::Text("devbench host present (build %u); port pending in runtime.json", DevBench::HostBuild());
						}
						ImGuiMCP::TextDisabled("Tool: floatingdamage.stats (MCP + REST); event: floatingdamage.sessionEnded");
					} else {
						ImGuiMCP::TextDisabled("devbench host not detected — install the devbench SKSE plugin.");
					}
				}
				ImGuiMCP::EndDisabled();
			}

			if (ImGuiMCP::CollapsingHeader("Advanced", 0)) {
				ImGuiMCP::Checkbox("Debug log", &s->debugLog);
				Tip("Writes per-event capture traces to FloatingDamageNG.log. For bug reports.");
				ImGuiMCP::Checkbox("Delta audit", &s->deltaAudit);
				Tip("Once a second, compares each fighter's observed health change against captured events and warns in the log about damage the mod missed.");
			}

			ImGuiMCP::Separator();
			if (ImGuiMCP::Button("Save to INI", { 0, 0 })) {
				s->Save();
			}
			ImGuiMCP::SameLine(0.0f, -1.0f);
			if (ImGuiMCP::Button("Reload INI", { 0, 0 })) {
				s->Load();
			}
			ImGuiMCP::SameLine(0.0f, -1.0f);
			if (ImGuiMCP::Button("Reset to defaults", { 0, 0 })) {
				s->ResetToDefaults();
			}
			ImGuiMCP::TextDisabled("Changes apply immediately; Save writes them to the INI. Font changes need a restart.");
		}

		void DrawCombatantTable(const CombatLog::SessionSummary& a_session)
		{
			constexpr auto flags = ImGuiMCP::ImGuiTableFlags_RowBg | ImGuiMCP::ImGuiTableFlags_BordersInnerH | ImGuiMCP::ImGuiTableFlags_SizingStretchProp;
			const auto id = std::format("##fdng_tbl{}", a_session.index);
			if (!ImGuiMCP::BeginTable(id.c_str(), 7, flags, { 0, 0 }, 0.0f)) {
				return;
			}
			ImGuiMCP::TableSetupColumn("Combatant", 0, 0.30f, 0);
			ImGuiMCP::TableSetupColumn("Dealt", 0, 0.12f, 0);
			ImGuiMCP::TableSetupColumn("DPS", 0, 0.10f, 0);
			ImGuiMCP::TableSetupColumn("Crit%", 0, 0.10f, 0);
			ImGuiMCP::TableSetupColumn("Taken", 0, 0.12f, 0);
			ImGuiMCP::TableSetupColumn("Healed", 0, 0.12f, 0);
			ImGuiMCP::TableSetupColumn("Fate", 0, 0.14f, 0);
			ImGuiMCP::TableHeadersRow();

			const float duration = std::max(a_session.duration, 0.01f);
			for (const auto& c : a_session.combatants) {  // pre-sorted by damage dealt
				ImGuiMCP::TableNextRow(0, 0.0f);
				ImGuiMCP::TableSetColumnIndex(0);
				ImGuiMCP::Text("%s%s", c.name.c_str(), c.isFollower ? " (follower)" : (c.isHostileToPlayer ? "" : " (neutral)"));
				ImGuiMCP::TableSetColumnIndex(1);
				ImGuiMCP::Text("%.0f", c.damageDealt);
				ImGuiMCP::TableSetColumnIndex(2);
				ImGuiMCP::Text("%.1f", c.damageDealt / duration);
				ImGuiMCP::TableSetColumnIndex(3);
				ImGuiMCP::Text("%d", c.hitsDealt > 0 ? (100 * c.critsDealt / c.hitsDealt) : 0);
				ImGuiMCP::TableSetColumnIndex(4);
				ImGuiMCP::Text("%.0f", c.damageTaken);
				ImGuiMCP::TableSetColumnIndex(5);
				if (c.healingReceived > 0.0f) {
					ImGuiMCP::Text("+%.0f", c.healingReceived);
				}
				ImGuiMCP::TableSetColumnIndex(6);
				if (c.died) {
					if (c.timeToDie >= 0.0f) {
						ImGuiMCP::Text("died (%.1fs)", c.timeToDie);
					} else {
						ImGuiMCP::Text("died");
					}
				} else if (c.fled) {
					ImGuiMCP::TextDisabled("fled");
				}
			}
			ImGuiMCP::EndTable();
		}

		void __stdcall RenderStats()
		{
			const auto live = CombatLog::GetSingleton()->GetLiveStats();
			if (live.active) {
				ImGuiMCP::Text("In combat: %.1fs | %.0f dmg | DPS %.1f real / %.1f active",
					live.sessionSeconds, live.playerDamage, live.realDPS, live.activeDPS);
			} else if (!Settings::GetSingleton()->enableCombatLog) {
				ImGuiMCP::TextDisabled("Combat log is disabled — enable it in Settings > Analytics.");
			} else {
				ImGuiMCP::TextDisabled("Not in combat.");
			}
			ImGuiMCP::Separator();

			const auto history = CombatLog::GetSingleton()->GetHistory();
			if (history.empty()) {
				ImGuiMCP::TextDisabled("No finished combat sessions yet.");
				return;
			}
			// Newest first.
			for (auto it = history.rbegin(); it != history.rend(); ++it) {
				const auto& s = *it;
				const auto header = std::format("#{} — {} @ {} — {:.1f}s, {:.0f} dmg###fdng_session{}",
					s.index, s.startedAt, s.location, s.duration, s.playerDamage, s.index);
				if (!ImGuiMCP::CollapsingHeader(header.c_str(), 0)) {
					continue;
				}
				ImGuiMCP::Text("Player DPS %.1f real / %.1f active", s.realDPS, s.activeDPS);
				if (!s.dpsSamples.empty()) {
					const auto overlay = std::format("player dps, peak {:.0f}", s.peakDPS);
					ImGuiMCP::PlotLines("##dps", s.dpsSamples.data(), static_cast<int>(s.dpsSamples.size()),
						0, overlay.c_str(), 0.0f, FLT_MAX, { -1.0f, 80.0f }, sizeof(float));
				}
				DrawCombatantTable(s);
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework not installed; in-game config UI disabled.");
			return;
		}
		SKSEMenuFramework::SetSection("FloatingDamageNG");
		SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
		SKSEMenuFramework::AddSectionItem("Combat Stats", RenderStats);
		logger::info("Registered SMF pages (framework v{:.1f}).", SKSEMenuFramework::GetMenuFrameworkVersion());
	}
}
