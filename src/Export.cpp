// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "Export.h"

#include "Settings.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace FDNG::Export
{
	namespace
	{
		std::filesystem::path ExportPath(const char* a_filename)
		{
			auto dir = SKSE::log::log_directory();
			return dir ? *dir / a_filename : std::filesystem::path{ a_filename };
		}

		nlohmann::json RowsToJson(const std::vector<CombatLog::BreakdownRow>& a_rows)
		{
			auto rows = nlohmann::json::array();
			for (const auto& r : a_rows) {
				rows.push_back({
					{ "name", r.name },
					{ "total", r.total },
					{ "mitigated", r.mitigated },
					{ "hits", r.hits },
					{ "crits", r.crits },
				});
			}
			return rows;
		}

		// One CSV cell; quotes anything that would break the row.
		std::string CsvCell(std::string_view a_value)
		{
			if (a_value.find_first_of(",\"\n") == std::string_view::npos) {
				return std::string{ a_value };
			}
			std::string quoted{ "\"" };
			for (const char c : a_value) {
				quoted += c;
				if (c == '"') {
					quoted += '"';
				}
			}
			quoted += '"';
			return quoted;
		}
	}

	nlohmann::json SessionToJson(const CombatLog::SessionSummary& a_s)
	{
		auto combatants = nlohmann::json::array();
		for (const auto& c : a_s.combatants) {
			combatants.push_back({
				{ "name", c.name },
				{ "follower", c.isFollower },
				{ "hostile", c.isHostileToPlayer },
				{ "died", c.died },
				{ "fled", c.fled },
				{ "damageDealt", c.damageDealt },
				{ "damageTaken", c.damageTaken },
				{ "healingReceived", c.healingReceived },
				{ "hits", c.hitsDealt },
				{ "crits", c.critsDealt },
				{ "timeToDie", c.timeToDie },
				{ "bySource", RowsToJson(c.bySource) },
				{ "byTarget", RowsToJson(c.byTarget) },
				{ "takenByKind", RowsToJson(c.takenByKind) },
				{ "killedBy", c.killedBy },
				{ "deathRecap", c.deathRecap },
			});
		}
		return {
			{ "index", a_s.index },
			{ "startedAt", a_s.startedAt },
			{ "location", a_s.location },
			{ "durationSeconds", a_s.duration },
			{ "playerDamage", a_s.playerDamage },
			{ "realDPS", a_s.realDPS },
			{ "activeDPS", a_s.activeDPS },
			{ "peakDPS", a_s.peakDPS },
			{ "dpsSamples", a_s.dpsSamples },
			{ "combatants", std::move(combatants) },
		};
	}

	void RotateIfOversized(const std::filesystem::path& a_path)
	{
		// One .old generation so a long-running install never grows an
		// unbounded file.
		constexpr std::uintmax_t kMaxBytes = 5ull * 1024 * 1024;
		std::error_code ec;
		if (std::filesystem::file_size(a_path, ec) > kMaxBytes && !ec) {
			auto old = a_path;
			old += ".old";
			std::filesystem::remove(old, ec);
			std::filesystem::rename(a_path, old, ec);
		}
	}

	void WriteSession(const CombatLog::SessionSummary& a_summary)
	{
		const auto settings = Settings::GetSingleton();

		if (settings->exportJsonl) {
			const auto path = ExportPath("FloatingDamageNG-sessions.jsonl");
			RotateIfOversized(path);
			if (std::ofstream out(path, std::ios::app); out) {
				out << SessionToJson(a_summary).dump() << '\n';
			} else {
				logger::warn("Could not open the JSONL export for writing.");
			}
		}

		if (settings->exportCsv) {
			const auto path = ExportPath("FloatingDamageNG-combatants.csv");
			RotateIfOversized(path);
			std::error_code ec;
			const bool writeHeader = std::filesystem::file_size(path, ec) == 0 || ec;
			std::ofstream out(path, std::ios::app);
			if (!out) {
				logger::warn("Could not open the CSV export for writing.");
				return;
			}
			if (writeHeader) {
				out << "session,started_at,location,duration_s,combatant,follower,hostile,"
					   "damage_dealt,damage_taken,healing_received,hits,crits,died,fled,time_to_die_s,killed_by\n";
			}
			// One row per combatant — the grain spreadsheets actually pivot on.
			for (const auto& c : a_summary.combatants) {
				out << std::format("{},{},{},{:.1f},{},{},{},{:.0f},{:.0f},{:.0f},{},{},{},{},{:.1f},{}\n",
					a_summary.index, CsvCell(a_summary.startedAt), CsvCell(a_summary.location), a_summary.duration,
					CsvCell(c.name), c.isFollower, c.isHostileToPlayer,
					c.damageDealt, c.damageTaken, c.healingReceived, c.hitsDealt, c.critsDealt,
					c.died, c.fled, c.timeToDie, CsvCell(c.killedBy));
			}
		}
	}
}
