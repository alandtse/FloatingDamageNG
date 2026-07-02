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

		void __stdcall RenderSettings()
		{
			auto* s = Settings::GetSingleton();

			ImGuiMCP::SeparatorText("Filters");
			ImGuiMCP::Checkbox("Player damage dealt", &s->showPlayerDamageDealt);
			ImGuiMCP::Checkbox("Follower damage dealt", &s->showFollowerDamageDealt);
			ImGuiMCP::Checkbox("NPC-on-NPC damage", &s->showNPCOnNPCDamage);
			ImGuiMCP::Checkbox("Player damage taken", &s->showPlayerDamageTaken);
			ImGuiMCP::Checkbox("Player numbers in first person (pinned)", &s->showFirstPersonNumbers);
			ImGuiMCP::Checkbox("Healing", &s->showHealing);
			ImGuiMCP::Checkbox("Magicka damage (hostile only)", &s->showMagickaDamage);
			ImGuiMCP::Checkbox("Stamina damage (hostile only)", &s->showStaminaDamage);
			ImGuiMCP::SliderFloat("NPC visibility radius (m)", &s->maxVisibilityRadiusMeters, 5.0f, 100.0f, "%.0f", 0);
			ImGuiMCP::SliderInt("Max concurrent numbers", &s->maxConcurrentQuads, 5, 128, "%d", 0);

			ImGuiMCP::SeparatorText("Sizing");
			ImGuiMCP::SliderFloat("Base font scale", &s->baseFontScale, 0.5f, 2.0f, "%.2f", 0);
			ImGuiMCP::SliderFloat("Damage-magnitude modifier", &s->logScaleModifier, 0.0f, 1.0f, "%.2f", 0);
			ImGuiMCP::SliderFloat("Scale ceiling", &s->maxFontScaleCeiling, 1.0f, 3.0f, "%.2f", 0);

			ImGuiMCP::SeparatorText("Motion");
			int profile = static_cast<int>(s->profile);
			const char* profiles[] = { "Float", "Arc (parabola)", "Radial burst" };
			if (ImGuiMCP::Combo("Kinematic profile", &profile, profiles, 3, -1)) {
				s->profile = static_cast<KinematicProfile>(profile);
			}
			ImGuiMCP::SliderFloat("Speed multiplier", &s->globalSpeedMultiplier, 0.25f, 3.0f, "%.2f", 0);
			ImGuiMCP::SliderFloat("Lifetime (s)", &s->quadLifetimeSeconds, 0.5f, 4.0f, "%.2f", 0);

			ImGuiMCP::SeparatorText("Behavior");
			ImGuiMCP::Checkbox("Show mitigation subtext", &s->showMitigation);
			ImGuiMCP::SliderFloat("Min damage to show", &s->minDamageToShow, 0.0f, 25.0f, "%.1f", 0);
			ImGuiMCP::SliderFloat("Min heal to show", &s->minHealToShow, 1.0f, 50.0f, "%.1f", 0);
			ImGuiMCP::SliderFloat("Tick merge window (s)", &s->dotAccumulationWindow, 0.1f, 2.0f, "%.2f", 0);

			ImGuiMCP::SeparatorText("Colors");
			ColorRow("Physical", s->colorPhysical);
			ColorRow("Critical", s->colorCritical);
			ColorRow("Blocked", s->colorBlocked);
			ColorRow("Fire", s->colorFire);
			ColorRow("Frost", s->colorFrost);
			ColorRow("Shock", s->colorShock);
			ColorRow("Poison", s->colorPoison);
			ColorRow("Magic (untyped)", s->colorMagic);
			ColorRow("Healing", s->colorHealing);
			ColorRow("Magicka damage", s->colorMagickaDamage);
			ColorRow("Stamina damage", s->colorStaminaDamage);

			ImGuiMCP::SeparatorText("Analytics");
			ImGuiMCP::Checkbox("Combat log", &s->enableCombatLog);
			ImGuiMCP::Checkbox("Write sessions to disk", &s->writeLogToDisk);
			ImGuiMCP::Checkbox("Live DPS window (flat)", &s->enableLiveDPSWindow);
			ImGuiMCP::Checkbox("Log follower performance", &s->logFollowerPerformance);
			if (ImGuiMCP::Checkbox("devbench integration (exposes stats on its local port)", &s->enableDevBench) && s->enableDevBench) {
				DevBench::Connect();
			}
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

			ImGuiMCP::SeparatorText("Debug");
			ImGuiMCP::Checkbox("Debug log", &s->debugLog);
			ImGuiMCP::Checkbox("Delta audit (missed-damage detector)", &s->deltaAudit);

			ImGuiMCP::Separator();
			if (ImGuiMCP::Button("Save to INI", { 0, 0 })) {
				s->Save();
			}
			ImGuiMCP::SameLine(0.0f, -1.0f);
			if (ImGuiMCP::Button("Reload INI", { 0, 0 })) {
				s->Load();
			}
			ImGuiMCP::TextDisabled("Changes apply immediately; font changes need a restart.");
		}

		void __stdcall RenderStats()
		{
			const auto live = CombatLog::GetSingleton()->GetLiveStats();
			if (live.active) {
				ImGuiMCP::Text("In combat: %.1fs | %.0f dmg | DPS %.1f real / %.1f active",
					live.sessionSeconds, live.playerDamage, live.realDPS, live.activeDPS);
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
				ImGuiMCP::Text("DPS %.1f real / %.1f active", s.realDPS, s.activeDPS);
				if (!s.dpsSamples.empty()) {
					ImGuiMCP::PlotLines("##dps", s.dpsSamples.data(), static_cast<int>(s.dpsSamples.size()),
						0, "player damage / second", 0.0f, FLT_MAX, { -1.0f, 80.0f }, sizeof(float));
				}
				for (const auto& line : s.combatantLines) {
					ImGuiMCP::BulletText("%s", line.c_str());
				}
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
