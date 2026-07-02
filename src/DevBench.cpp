// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "DevBench.h"

#include <DevBenchAPI.h>
#include <nlohmann/json.hpp>

namespace FDNG::DevBench
{
	namespace
	{
		DevBenchAPI::IDevBenchInterface001* g_devbench = nullptr;

		nlohmann::json SummaryToJson(const CombatLog::SessionSummary& a_s)
		{
			return {
				{ "index", a_s.index },
				{ "startedAt", a_s.startedAt },
				{ "location", a_s.location },
				{ "durationSeconds", a_s.duration },
				{ "playerDamage", a_s.playerDamage },
				{ "realDPS", a_s.realDPS },
				{ "activeDPS", a_s.activeDPS },
				{ "dpsSamples", a_s.dpsSamples },
				{ "combatants", a_s.combatantLines },
			};
		}

		// Runs on devbench's listener thread — reads only FloatingDamageNG's
		// own mutex-guarded copies, never game state, so no main-thread
		// marshaling is needed.
		void StatsHandler(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			std::string action = "summary";
			if (a_argsJson) {
				const auto args = nlohmann::json::parse(a_argsJson, nullptr, false);
				if (args.is_object() && args.contains("action") && args["action"].is_string()) {
					action = args["action"].get<std::string>();
				}
			}

			nlohmann::json result;
			const auto live = CombatLog::GetSingleton()->GetLiveStats();
			result["live"] = {
				{ "inCombat", live.active },
				{ "sessionSeconds", live.sessionSeconds },
				{ "playerDamage", live.playerDamage },
				{ "realDPS", live.realDPS },
				{ "activeDPS", live.activeDPS },
			};
			if (action == "sessions" || action == "summary") {
				auto sessions = nlohmann::json::array();
				for (const auto& s : CombatLog::GetSingleton()->GetHistory()) {
					auto j = SummaryToJson(s);
					if (action == "summary") {
						j.erase("dpsSamples");  // keep the summary compact
						j.erase("combatants");
					}
					sessions.push_back(std::move(j));
				}
				result["sessions"] = std::move(sessions);
			}
			a_write(a_sink, result.dump().c_str());
		}
	}

	void Connect()
	{
		g_devbench = DevBenchAPI::GetDevBenchInterface001();
		if (!g_devbench) {
			logger::info("devbench not installed; stats tool not registered.");
			return;
		}

		constexpr auto descriptor = R"({
			"description": "FloatingDamageNG combat analytics: live DPS and finished combat sessions (per-combatant damage, healing, crits, time-to-die). action=summary omits per-session detail; action=sessions includes DPS timelines and combatant breakdowns.",
			"inputSchema": {
				"type": "object",
				"properties": {
					"action": { "type": "string", "enum": ["summary", "sessions", "live"] }
				}
			},
			"readOnly": true
		})";
		if (g_devbench->RegisterTool("floatingdamage.stats", descriptor, &StatsHandler, nullptr)) {
			logger::info("Registered devbench tool floatingdamage.stats (host build {}).", g_devbench->GetBuildNumber());
		}
	}

	void NotifySessionEnded(const CombatLog::SessionSummary& a_summary)
	{
		if (!g_devbench) {
			return;
		}
		auto payload = SummaryToJson(a_summary);
		payload.erase("dpsSamples");  // event stream stays light; fetch detail via the tool
		g_devbench->EmitEvent("floatingdamage.sessionEnded", payload.dump().c_str());
	}
}
