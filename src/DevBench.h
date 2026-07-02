// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#include "CombatLog.h"

namespace FDNG::DevBench
{
	// Optional devbench integration: registers a `floatingdamage.stats`
	// MCP/REST tool (live stats + session history as JSON) and emits
	// session-boundary events. No-op when devbench isn't installed — the
	// interface handshake simply returns null. Call at kPostPostLoad.
	void Connect();

	// Publish a finished session to /api/events (topic
	// "floatingdamage.sessionEnded"). Safe to call when not connected.
	void NotifySessionEnded(const CombatLog::SessionSummary& a_summary);
}
