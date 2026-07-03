// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "CombatLog.h"

#include <nlohmann/json_fwd.hpp>

// Session export. There is no cross-game combat-log standard to target
// (WarcraftLogs/ESOLogs validate against their game databases; ACT needs a
// bespoke C# plugin), so the formats are what analysis tooling actually
// ingests: JSON Lines for pandas/jq/dashboards and flat CSV for
// spreadsheets. SessionToJson is the single canonical serialization —
// devbench and the JSONL file both emit it; CSV flattens the same summary.
namespace FDNG::Export
{
	// Canonical structured form of a finished session.
	nlohmann::json SessionToJson(const CombatLog::SessionSummary& a_summary);

	// Append the session to the exports enabled in settings (JSONL/CSV).
	// Main thread (called from CombatLog::CloseSession).
	void WriteSession(const CombatLog::SessionSummary& a_summary);

	// Rotate a_path to a_path + ".old" once it exceeds the size cap; one
	// generation kept. Shared by every append-forever file this plugin writes.
	void RotateIfOversized(const std::filesystem::path& a_path);
}
