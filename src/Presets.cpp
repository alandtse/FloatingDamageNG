// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "Presets.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
#include <optional>

namespace FDNG::Presets
{
	namespace
	{
		constexpr auto kPresetDir = "Data/SKSE/Plugins/FloatingDamageNG/Presets";

		std::vector<Effect> g_cache;
		bool g_loaded = false;

		// Keep a filename to letters, digits, space, dash, underscore so a
		// preset name can't escape the directory.
		std::string SanitizeFileName(std::string_view a_name)
		{
			std::string out;
			for (const char c : a_name) {
				if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') {
					out += c;
				}
			}
			// Trim surrounding spaces.
			const auto begin = out.find_first_not_of(' ');
			const auto end = out.find_last_not_of(' ');
			return begin == std::string::npos ? std::string{} : out.substr(begin, end - begin + 1);
		}

		std::optional<Effect> ParseFile(const std::filesystem::path& a_path)
		{
			std::ifstream in(a_path);
			if (!in) {
				return std::nullopt;
			}
			const auto json = nlohmann::json::parse(in, nullptr, false);
			if (!json.is_object()) {
				logger::warn("Preset '{}' is not a JSON object; skipped.", a_path.filename().string());
				return std::nullopt;
			}
			// Values default to a plain float rise if a field is missing, and
			// are clamped to the same ranges as the INI.
			const auto num = [&](const char* a_key, float a_def) {
				return json.contains(a_key) && json[a_key].is_number() ? json[a_key].get<float>() : a_def;
			};
			Effect e;
			e.name = json.contains("name") && json["name"].is_string() ? json["name"].get<std::string>() : a_path.stem().string();
			e.motion.riseSpeed = num("riseSpeed", 45.0f);
			e.motion.riseAccel = num("riseAccel", 0.0f);
			e.motion.lateralSpeed = num("lateralSpeed", 0.0f);
			e.motion.lateralDamping = std::clamp(num("lateralDamping", 0.0f), 0.0f, 20.0f);
			e.spread = static_cast<SpreadPattern>(std::clamp(static_cast<int>(num("spreadPattern", 0.0f)), 0, 2));
			e.spawnAngleDeg = num("spawnAngle", 0.0f);
			return e;
		}

		void LoadIntoCache()
		{
			g_cache.clear();
			for (const auto& b : kEffectPresets) {
				g_cache.push_back({ b.name, true, b.motion, b.spread, b.spawnAngleDeg });
			}

			std::error_code ec;
			if (!std::filesystem::is_directory(kPresetDir, ec)) {
				return;
			}
			std::vector<Effect> user;
			for (const auto& entry : std::filesystem::directory_iterator(kPresetDir, ec)) {
				if (ec || !entry.is_regular_file() || entry.path().extension() != ".json") {
					continue;
				}
				if (auto e = ParseFile(entry.path())) {
					user.push_back(std::move(*e));
				}
			}
			std::sort(user.begin(), user.end(), [](const Effect& a, const Effect& b) { return a.name < b.name; });
			for (auto& e : user) {
				g_cache.push_back(std::move(e));
			}
			logger::info("Loaded {} user motion preset(s).", user.size());
		}
	}

	const std::vector<Effect>& All()
	{
		if (!g_loaded) {
			LoadIntoCache();
			g_loaded = true;
		}
		return g_cache;
	}

	void Reload()
	{
		LoadIntoCache();
		g_loaded = true;
	}

	bool Save(const Effect& a_effect)
	{
		const auto file = SanitizeFileName(a_effect.name);
		if (file.empty()) {
			return false;
		}
		std::error_code ec;
		std::filesystem::create_directories(kPresetDir, ec);

		const nlohmann::json json{
			{ "name", a_effect.name },
			{ "riseSpeed", a_effect.motion.riseSpeed },
			{ "riseAccel", a_effect.motion.riseAccel },
			{ "lateralSpeed", a_effect.motion.lateralSpeed },
			{ "lateralDamping", a_effect.motion.lateralDamping },
			{ "spreadPattern", std::to_underlying(a_effect.spread) },
			{ "spawnAngle", a_effect.spawnAngleDeg },
		};
		std::ofstream out(std::filesystem::path(kPresetDir) / (file + ".json"));
		if (!out) {
			logger::warn("Could not write preset '{}'.", file);
			return false;
		}
		out << json.dump(2) << '\n';
		Reload();
		return true;
	}
}
