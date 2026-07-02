// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "Settings.h"

#include <SimpleIni.h>
#include <spdlog/spdlog.h>

namespace FDNG
{
	namespace
	{
		constexpr auto kIniPath = L"Data/SKSE/Plugins/FloatingDamageNG.ini";

		std::uint32_t GetHexColor(const CSimpleIniA& a_ini, const char* a_section, const char* a_key, std::uint32_t a_default)
		{
			const char* raw = a_ini.GetValue(a_section, a_key);
			if (!raw) {
				return a_default;
			}
			std::string_view sv{ raw };
			if (sv.starts_with("0x") || sv.starts_with("0X")) {
				sv.remove_prefix(2);
			} else if (sv.starts_with('#')) {
				sv.remove_prefix(1);
			}
			std::uint32_t value = 0;
			const auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value, 16);
			return ec == std::errc() ? value : a_default;
		}

		void AddDefaultLocationTag(Settings& a_settings)
		{
			// Skeleton head nodes are "NPC Head [Head]" on humanoids; most
			// creature skeletons also carry "Head" in the node name.
			a_settings.locationTags.push_back({ std::regex{ ".*head.*", std::regex::icase }, "HEADSHOT" });
		}
	}

	Settings* Settings::GetSingleton()
	{
		static Settings singleton;
		return &singleton;
	}

	void Settings::Load()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		if (ini.LoadFile(kIniPath) < 0) {
			logger::info("No FloatingDamageNG.ini found; using defaults.");
			return;
		}

		showPlayerDamageDealt = ini.GetBoolValue("CoreFilters", "bShowPlayerDamageDealt", showPlayerDamageDealt);
		showFollowerDamageDealt = ini.GetBoolValue("CoreFilters", "bShowFollowerDamageDealt", showFollowerDamageDealt);
		showNPCOnNPCDamage = ini.GetBoolValue("CoreFilters", "bShowNPCOnNPCDamage", showNPCOnNPCDamage);
		showPlayerDamageTaken = ini.GetBoolValue("CoreFilters", "bShowPlayerDamageTaken", showPlayerDamageTaken);
		showHealing = ini.GetBoolValue("CoreFilters", "bShowHealing", showHealing);
		showMagickaDamage = ini.GetBoolValue("CoreFilters", "bShowMagickaDamage", showMagickaDamage);
		showStaminaDamage = ini.GetBoolValue("CoreFilters", "bShowStaminaDamage", showStaminaDamage);
		maxVisibilityRadiusMeters = static_cast<float>(ini.GetDoubleValue("CoreFilters", "fMaxVisibilityRadius", maxVisibilityRadiusMeters));
		maxConcurrentQuads = static_cast<int>(ini.GetLongValue("CoreFilters", "iMaxConcurrentQuads", maxConcurrentQuads));

		baseFontScale = static_cast<float>(ini.GetDoubleValue("DynamicSizing", "fBaseFontScale", baseFontScale));
		logScaleModifier = static_cast<float>(ini.GetDoubleValue("DynamicSizing", "fLogScaleModifier", logScaleModifier));
		maxFontScaleCeiling = static_cast<float>(ini.GetDoubleValue("DynamicSizing", "fMaxFontScaleCeiling", maxFontScaleCeiling));

		profile = static_cast<KinematicProfile>(std::clamp<long>(ini.GetLongValue("KinematicProfiles", "iSelectedProfile", std::to_underlying(profile)), 0, 2));
		globalSpeedMultiplier = static_cast<float>(ini.GetDoubleValue("KinematicProfiles", "fGlobalSpeedMultiplier", globalSpeedMultiplier));
		quadLifetimeSeconds = static_cast<float>(ini.GetDoubleValue("KinematicProfiles", "fQuadLifetimeSeconds", quadLifetimeSeconds));

		showMitigation = ini.GetBoolValue("Behavior", "bShowMitigation", showMitigation);
		minDamageToShow = static_cast<float>(ini.GetDoubleValue("Behavior", "fMinDamageToShow", minDamageToShow));
		minHealToShow = static_cast<float>(ini.GetDoubleValue("Behavior", "fMinHealToShow", minHealToShow));
		dotAccumulationWindow = static_cast<float>(ini.GetDoubleValue("Behavior", "fDotAccumulationWindow", dotAccumulationWindow));

		originStyle = static_cast<OriginStyle>(std::clamp<long>(ini.GetLongValue("Style", "iOriginStyle", std::to_underlying(originStyle)), 0, 2));
		styleThickness = std::clamp(static_cast<float>(ini.GetDoubleValue("Style", "fStyleThickness", styleThickness)), 0.5f, 6.0f);

		for (const auto& def : kColorTable) {
			this->*def.field = GetHexColor(ini, "Colors", def.iniKey, this->*def.field);
		}

		if (const char* font = ini.GetValue("Font", "sFontPath"); font && font[0] != '\0') {
			fontPath = font;
		}

		showFirstPersonNumbers = ini.GetBoolValue("FirstPerson", "bShowFirstPersonNumbers", showFirstPersonNumbers);
		firstPersonX = std::clamp(static_cast<float>(ini.GetDoubleValue("FirstPerson", "fPositionX", firstPersonX)), 0.0f, 1.0f);
		firstPersonY = std::clamp(static_cast<float>(ini.GetDoubleValue("FirstPerson", "fPositionY", firstPersonY)), 0.0f, 1.0f);

		showHitLocation = ini.GetBoolValue("Locational", "bShowHitLocation", showHitLocation);
		showAmplification = ini.GetBoolValue("Locational", "bShowAmplification", showAmplification);
		amplificationThreshold = static_cast<float>(ini.GetDoubleValue("Locational", "fAmplificationThreshold", amplificationThreshold));
		locationTags.clear();
		for (int i = 1; i <= 8; ++i) {
			const char* pattern = ini.GetValue("Locational", std::format("sPattern{}", i).c_str());
			const char* label = ini.GetValue("Locational", std::format("sLabel{}", i).c_str());
			if (!pattern || !pattern[0] || !label || !label[0]) {
				continue;
			}
			try {
				locationTags.push_back({ std::regex{ pattern, std::regex::icase }, label });
			} catch (const std::regex_error&) {
				logger::warn("Invalid location pattern '{}' ignored.", pattern);
			}
		}
		if (locationTags.empty()) {
			AddDefaultLocationTag(*this);
		}

		enableCombatLog = ini.GetBoolValue("Analytics", "bEnableCombatLog", enableCombatLog);
		writeLogToDisk = ini.GetBoolValue("Analytics", "bWriteLogToDisk", writeLogToDisk);
		enableLiveDPSWindow = ini.GetBoolValue("Analytics", "bEnableLiveDPSWindow", enableLiveDPSWindow);
		logFollowerPerformance = ini.GetBoolValue("Analytics", "bLogFollowerPerformance", logFollowerPerformance);
		postCombatWindowFadeSeconds = static_cast<float>(ini.GetDoubleValue("Analytics", "fPostCombatWindowFadeSeconds", postCombatWindowFadeSeconds));
		enableDevBench = ini.GetBoolValue("Analytics", "bEnableDevBench", enableDevBench);

		debugLog = ini.GetBoolValue("Debug", "bDebugLog", debugLog);
		deltaAudit = ini.GetBoolValue("Debug", "bDeltaAudit", deltaAudit);

		// The default sink filters at info, which would swallow the debug
		// traces bDebugLog exists to produce.
		if (const auto log = spdlog::default_logger()) {
			log->set_level(debugLog ? spdlog::level::debug : spdlog::level::info);
			log->flush_on(debugLog ? spdlog::level::debug : spdlog::level::info);
		}

		logger::info("Settings loaded (profile={}, lifetime={:.2f}s, maxQuads={}).",
			std::to_underlying(profile), quadLifetimeSeconds, maxConcurrentQuads);
	}

	void Settings::ResetToDefaults()
	{
		*this = Settings{};
		AddDefaultLocationTag(*this);
		if (const auto log = spdlog::default_logger()) {
			log->set_level(spdlog::level::info);
			log->flush_on(spdlog::level::info);
		}
	}

	void Settings::Save() const
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		ini.LoadFile(kIniPath);  // keep unknown keys/comments where possible

		ini.SetLongValue("Style", "iOriginStyle", std::to_underlying(originStyle));
		ini.SetDoubleValue("Style", "fStyleThickness", styleThickness);

		for (const auto& def : kColorTable) {
			ini.SetValue("Colors", def.iniKey, std::format("0x{:06X}", this->*def.field).c_str());
		}

		ini.SetBoolValue("CoreFilters", "bShowPlayerDamageDealt", showPlayerDamageDealt);
		ini.SetBoolValue("CoreFilters", "bShowFollowerDamageDealt", showFollowerDamageDealt);
		ini.SetBoolValue("CoreFilters", "bShowNPCOnNPCDamage", showNPCOnNPCDamage);
		ini.SetBoolValue("CoreFilters", "bShowPlayerDamageTaken", showPlayerDamageTaken);
		ini.SetBoolValue("CoreFilters", "bShowHealing", showHealing);
		ini.SetBoolValue("CoreFilters", "bShowMagickaDamage", showMagickaDamage);
		ini.SetBoolValue("CoreFilters", "bShowStaminaDamage", showStaminaDamage);
		ini.SetDoubleValue("CoreFilters", "fMaxVisibilityRadius", maxVisibilityRadiusMeters);
		ini.SetLongValue("CoreFilters", "iMaxConcurrentQuads", maxConcurrentQuads);

		ini.SetDoubleValue("DynamicSizing", "fBaseFontScale", baseFontScale);
		ini.SetDoubleValue("DynamicSizing", "fLogScaleModifier", logScaleModifier);
		ini.SetDoubleValue("DynamicSizing", "fMaxFontScaleCeiling", maxFontScaleCeiling);

		ini.SetValue("Font", "sFontPath", fontPath.c_str());

		ini.SetBoolValue("FirstPerson", "bShowFirstPersonNumbers", showFirstPersonNumbers);
		ini.SetDoubleValue("FirstPerson", "fPositionX", firstPersonX);
		ini.SetDoubleValue("FirstPerson", "fPositionY", firstPersonY);

		ini.SetBoolValue("Locational", "bShowHitLocation", showHitLocation);
		ini.SetBoolValue("Locational", "bShowAmplification", showAmplification);
		ini.SetDoubleValue("Locational", "fAmplificationThreshold", amplificationThreshold);
		// Patterns are load-only (std::regex can't round-trip its source);
		// leave whatever the user wrote in place.

		ini.SetLongValue("KinematicProfiles", "iSelectedProfile", std::to_underlying(profile));
		ini.SetDoubleValue("KinematicProfiles", "fGlobalSpeedMultiplier", globalSpeedMultiplier);
		ini.SetDoubleValue("KinematicProfiles", "fQuadLifetimeSeconds", quadLifetimeSeconds);

		ini.SetBoolValue("Behavior", "bShowMitigation", showMitigation);
		ini.SetDoubleValue("Behavior", "fMinDamageToShow", minDamageToShow);
		ini.SetDoubleValue("Behavior", "fMinHealToShow", minHealToShow);
		ini.SetDoubleValue("Behavior", "fDotAccumulationWindow", dotAccumulationWindow);

		ini.SetBoolValue("Analytics", "bEnableCombatLog", enableCombatLog);
		ini.SetBoolValue("Analytics", "bWriteLogToDisk", writeLogToDisk);
		ini.SetBoolValue("Analytics", "bEnableLiveDPSWindow", enableLiveDPSWindow);
		ini.SetBoolValue("Analytics", "bLogFollowerPerformance", logFollowerPerformance);
		ini.SetDoubleValue("Analytics", "fPostCombatWindowFadeSeconds", postCombatWindowFadeSeconds);
		ini.SetBoolValue("Analytics", "bEnableDevBench", enableDevBench);

		ini.SetBoolValue("Debug", "bDebugLog", debugLog);
		ini.SetBoolValue("Debug", "bDeltaAudit", deltaAudit);

		if (ini.SaveFile(kIniPath) < 0) {
			logger::warn("Failed to save FloatingDamageNG.ini");
		} else {
			logger::info("Settings saved to INI.");
		}
	}
}
