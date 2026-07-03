// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "Fonts.h"

#include "Settings.h"

#include <cctype>

namespace FDNG::Fonts
{
	namespace
	{
		// Candidate TTFs, first hit wins: user-configured, a mod-shipped
		// override, then bold Windows system fonts (always present).
		std::vector<std::string> CandidatePaths()
		{
			std::vector<std::string> paths;
			if (const auto& user = Settings::GetSingleton()->fontPath; !user.empty()) {
				paths.push_back(user);
			}
			paths.emplace_back("Data/Interface/FloatingDamageNG/FloatingDamageNG.ttf");

			char windir[MAX_PATH]{};
			if (GetEnvironmentVariableA("WINDIR", windir, MAX_PATH) > 0) {
				paths.push_back(std::format("{}\\Fonts\\arialbd.ttf", windir));
				paths.push_back(std::format("{}\\Fonts\\segoeuib.ttf", windir));
			}
			return paths;
		}
	}

	void Load()
	{
		auto& io = ImGui::GetIO();
		for (const auto& path : CandidatePaths()) {
			if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
				continue;
			}
			// The size is only the default; draw calls request their own size
			// and the atlas rasterizes each one from the vector outlines.
			if (ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), 48.0f)) {
				io.FontDefault = font;
				logger::info("Loaded damage font '{}'.", path);
				return;
			}
		}
		logger::warn("No TTF font found; falling back to the embedded bitmap font (numbers will look pixelated).");
	}

	const std::vector<std::pair<std::string, std::string>>& Available()
	{
		static std::vector<std::pair<std::string, std::string>> fonts = [] {
			std::vector<std::pair<std::string, std::string>> out;
			const auto scan = [&](const std::filesystem::path& a_dir) {
				std::error_code ec;
				if (!std::filesystem::is_directory(a_dir, ec)) {
					return;
				}
				for (const auto& e : std::filesystem::directory_iterator(a_dir, ec)) {
					if (ec || !e.is_regular_file()) {
						continue;
					}
					auto ext = e.path().extension().string();
					std::ranges::transform(ext, ext.begin(), [](char c) { return static_cast<char>(std::tolower(c)); });
					if (ext == ".ttf" || ext == ".otf") {
						out.emplace_back(e.path().stem().string(), e.path().string());
					}
				}
			};
			scan("Data/Interface/FloatingDamageNG");
			char windir[MAX_PATH]{};
			if (GetEnvironmentVariableA("WINDIR", windir, MAX_PATH) > 0) {
				scan(std::filesystem::path(windir) / "Fonts");
			}
			std::ranges::sort(out, [](const auto& a, const auto& b) { return a.first < b.first; });
			return out;
		}();
		return fonts;
	}
}
