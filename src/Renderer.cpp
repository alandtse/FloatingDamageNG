// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#include "Renderer.h"

#include "CombatLog.h"
#include "Fonts.h"
#include "ImGuiVRHelperClientSDK.h"
#include "NumberManager.h"
#include "Settings.h"

namespace FDNG::Renderer
{
	namespace
	{
		ImGuiVRHelperPluginAPI::Client g_vrClient;

		std::atomic<bool> g_initialized{ false };
		ID3D11Device* g_d3dDevice = nullptr;
		ID3D11DeviceContext* g_d3dContext = nullptr;

		// Reused frame buffers (no steady-state allocation).
		std::vector<ResolvedNumber> g_resolved;
		std::vector<ImGuiVRHelperPluginAPI::WorldQuad> g_quads;

		constexpr float kBaseFontPx = 48.0f;
		constexpr float kSubtextRatio = 0.55f;
		constexpr float kPanelMarginPx = 16.0f;
		constexpr float kPanelGapPx = 24.0f;
		// World size of one panel pixel on a billboard (matches FloatingSubtitles' tuning).
		constexpr float kWorldMetersPerPanelPixel = 0.0016875f;

		ImU32 KindColor(const Number& a_n, float a_alpha)
		{
			const auto settings = Settings::GetSingleton();
			std::uint32_t rgb = settings->colorPhysical;
			if (a_n.flags.critical) {
				rgb = settings->colorCritical;
			} else if (a_n.flags.blocked) {
				rgb = settings->colorBlocked;
			} else {
				switch (a_n.kind) {
				case DamageKind::kFire:
					rgb = settings->colorFire;
					break;
				case DamageKind::kFrost:
					rgb = settings->colorFrost;
					break;
				case DamageKind::kShock:
					rgb = settings->colorShock;
					break;
				case DamageKind::kPoison:
					rgb = settings->colorPoison;
					break;
				case DamageKind::kMagic:
					rgb = settings->colorMagic;
					break;
				case DamageKind::kHealing:
					rgb = settings->colorHealing;
					break;
				case DamageKind::kMagickaDrain:
					rgb = settings->colorMagickaDamage;
					break;
				case DamageKind::kStaminaDrain:
					rgb = settings->colorStaminaDamage;
					break;
				default:
					break;
				}
			}
			const auto a = static_cast<std::uint8_t>(std::clamp(a_alpha, 0.0f, 1.0f) * 255.0f);
			return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, a);
		}

		bool PlayerInFirstPerson()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			return camera && camera->IsInFirstPerson();
		}

		// Small live DPS readout (spec §5); lingers for the configured fade
		// window after combat ends.
		void DrawLiveDPSWindow()
		{
			const auto settings = Settings::GetSingleton();
			if (!settings->enableCombatLog || !settings->enableLiveDPSWindow) {
				return;
			}
			const auto stats = CombatLog::GetSingleton()->GetLiveStats();
			if (!stats.active && stats.secondsSinceEnd > settings->postCombatWindowFadeSeconds) {
				return;
			}

			const float alpha = stats.active ? 0.85f : 0.85f * (1.0f - stats.secondsSinceEnd / settings->postCombatWindowFadeSeconds);
			const auto displaySize = ImGui::GetIO().DisplaySize;
			ImGui::SetNextWindowPos(ImVec2(displaySize.x - 20.0f, 20.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
			ImGui::SetNextWindowBgAlpha(alpha * 0.4f);
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
			if (ImGui::Begin("##FDNGDPS", nullptr,
					ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
						ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
				ImGui::Text("%.0f dmg  %.1fs", stats.playerDamage, stats.sessionSeconds);
				ImGui::Text("DPS %.1f real / %.1f active", stats.realDPS, stats.activeDPS);
			}
			ImGui::End();
			ImGui::PopStyleVar();
		}

		void AddOutlinedText(ImDrawList* a_drawList, ImFont* a_font, float a_size, ImVec2 a_pos, ImU32 a_color, std::uint8_t a_alpha, const char* a_text)
		{
			const ImU32 shadow = IM_COL32(0, 0, 0, a_alpha);
			a_drawList->AddText(a_font, a_size, ImVec2(a_pos.x + 2.0f, a_pos.y + 2.0f), shadow, a_text);
			a_drawList->AddText(a_font, a_size, a_pos, a_color, a_text);
		}

		// Draw one number (text + optional mitigation subtext) centered at
		// a_center.x, starting at a_center.y. Returns the block size drawn.
		ImVec2 DrawNumberBlock(ImDrawList* a_drawList, const ResolvedNumber& a_rn, ImVec2 a_topLeft, float a_fontPx)
		{
			const auto& n = *a_rn.number;
			ImFont* font = ImGui::GetFont();
			const float mainPx = a_fontPx;
			// The subtext grows with the resisted share, so a mostly-resisted
			// hit reads at a glance.
			float subRatio = kSubtextRatio;
			if (n.mitigated > 0.0f && n.amount > 0.0f) {
				const float resistShare = n.mitigated / (n.mitigated + n.amount);
				subRatio = 0.40f + 0.35f * resistShare;
			}
			const float subPx = a_fontPx * subRatio;

			const ImVec2 mainSz = font->CalcTextSizeA(mainPx, FLT_MAX, 0.0f, n.text);
			ImVec2 blockSz = mainSz;
			ImVec2 subSz{ 0.0f, 0.0f };
			if (n.subtext[0] != '\0') {
				subSz = font->CalcTextSizeA(subPx, FLT_MAX, 0.0f, n.subtext);
				blockSz.x = std::max(blockSz.x, subSz.x);
				blockSz.y += subSz.y;
			}

			const auto alpha = static_cast<std::uint8_t>(std::clamp(a_rn.alpha, 0.0f, 1.0f) * 255.0f);
			const ImU32 color = KindColor(n, a_rn.alpha);
			AddOutlinedText(a_drawList, font, mainPx,
				ImVec2(a_topLeft.x + (blockSz.x - mainSz.x) * 0.5f, a_topLeft.y), color, alpha, n.text);
			if (n.subtext[0] != '\0') {
				// Partial mitigation reads in the element's color too, dimmed.
				const ImU32 subColor = KindColor(n, a_rn.alpha * 0.8f);
				AddOutlinedText(a_drawList, font, subPx,
					ImVec2(a_topLeft.x + (blockSz.x - subSz.x) * 0.5f, a_topLeft.y + mainSz.y), subColor, alpha, n.subtext);
			}
			return blockSz;
		}

		// VR: lay each number into its own sub-rect of the helper panel and
		// record a world-space billboard pointing at that sub-rect.
		void DrawWorldQuadFrame()
		{
			g_quads.clear();

			const ImVec2 panelSize = ImGui::GetIO().DisplaySize;
			if (panelSize.x <= 0.0f || panelSize.y <= 0.0f) {
				return;
			}

			ImGui::SetNextWindowPos({ 0.0f, 0.0f });
			ImGui::SetNextWindowSize(panelSize);
			ImGui::Begin("##FDNG", nullptr,
				ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs);
			auto* drawList = ImGui::GetWindowDrawList();

			const auto settings = Settings::GetSingleton();
			const bool firstPerson = PlayerInFirstPerson();

			// First person: re-anchor player-received numbers ~1 m ahead at
			// chest height (fresh heading each frame) instead of on the head,
			// which would sit on the camera.
			const auto player = RE::PlayerCharacter::GetSingleton();
			RE::NiPoint3 firstPersonAnchor;
			if (firstPerson && player) {
				const float heading = player->GetAngleZ();
				firstPersonAnchor = player->GetPosition();
				firstPersonAnchor.x += std::sin(heading) * 70.0f;
				firstPersonAnchor.y += std::cos(heading) * 70.0f;
				firstPersonAnchor.z += 100.0f;
			}

			// Simple shelf packer across the panel.
			float penX = kPanelMarginPx;
			float penY = kPanelMarginPx;
			float rowH = 0.0f;

			for (const auto& rn : g_resolved) {
				RE::NiPoint3 worldPos = rn.worldPos;
				if (firstPerson && rn.number->origin == OriginTier::kPlayerVictim) {
					if (!settings->showFirstPersonNumbers || !player) {
						continue;
					}
					worldPos = firstPersonAnchor + (rn.worldPos - rn.number->anchor);  // keep spiral + kinematic motion
				}
				// Full alpha in the panel; the fade is baked into the text color,
				// so keep the drawn pixels and the quad in sync by drawing as-is.
				const float fontPx = kBaseFontPx * rn.scale;
				const ImVec2 estimate = ImGui::GetFont()->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, rn.number->text);
				float blockW = estimate.x + 8.0f;

				if (penX + blockW > panelSize.x - kPanelMarginPx) {
					penX = kPanelMarginPx;
					penY += rowH + kPanelGapPx;
					rowH = 0.0f;
				}
				if (penY + fontPx * 2.0f > panelSize.y - kPanelMarginPx) {
					break;  // panel full; drop the rest this frame
				}

				const ImVec2 blockSz = DrawNumberBlock(drawList, rn, ImVec2(penX, penY), fontPx);
				rowH = std::max(rowH, blockSz.y);

				const float heightMeters = blockSz.y * kWorldMetersPerPanelPixel;

				RE::NiPoint3 quadPos = worldPos;
				quadPos.z += 0.5f * heightMeters / kGameUnitToMeter;  // pos is the quad center

				ImGuiVRHelperPluginAPI::WorldQuad quad{};
				quad.u0 = penX / panelSize.x;
				quad.v0 = penY / panelSize.y;
				quad.u1 = std::min((penX + blockSz.x) / panelSize.x, 1.0f);
				quad.v1 = std::min((penY + blockSz.y) / panelSize.y, 1.0f);
				quad.pos[0] = quadPos.x;
				quad.pos[1] = quadPos.y;
				quad.pos[2] = quadPos.z;
				quad.height_m = heightMeters;
				g_quads.push_back(quad);

				penX += blockW + kPanelGapPx;
			}

			ImGui::End();
		}

		// Flat: project each number to the screen and draw it directly.
		void DrawFlatFrame()
		{
			const auto camera = RE::Main::WorldRootCamera();
			if (!camera) {
				return;
			}

			ImGui::SetNextWindowPos({ 0.0f, 0.0f });
			ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
			ImGui::Begin("##FDNG", nullptr,
				ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs);
			auto* drawList = ImGui::GetWindowDrawList();

			const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto playerPos = player ? player->GetPosition() : RE::NiPoint3{};

			const auto settings = Settings::GetSingleton();
			const bool firstPerson = PlayerInFirstPerson();

			for (const auto& rn : g_resolved) {
				ImVec2 screenPos;
				float fontPx;
				if (firstPerson && rn.number->origin == OriginTier::kPlayerVictim) {
					// First person: player numbers pin to a configurable screen
					// spot (they'd otherwise sit on the camera), keeping the
					// vertical component of their motion.
					if (!settings->showFirstPersonNumbers) {
						continue;
					}
					const float risePx = (rn.worldPos.z - rn.number->anchor.z) * 0.75f;
					screenPos = ImVec2(displaySize.x * settings->firstPersonX,
						displaySize.y * settings->firstPersonY - risePx);
					fontPx = kBaseFontPx * rn.scale;
				} else {
					float x = 0.0f, y = 0.0f, z = -1.0f;
					if (!camera->WorldPtToScreenPt3(rn.worldPos, x, y, z, 1e-5f) || z <= 0.0f) {
						continue;
					}
					screenPos = ImVec2(displaySize.x * x, displaySize.y * (1.0f - y));

					// Perspective size: full size inside ~3.5 m, shrinking with
					// distance.
					const float distMeters = std::max(playerPos.GetDistance(rn.worldPos) * kGameUnitToMeter, 0.1f);
					const float perspective = std::clamp(3.5f / distMeters, 0.25f, 1.25f);
					fontPx = kBaseFontPx * rn.scale * perspective;
				}

				const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fontPx, FLT_MAX, 0.0f, rn.number->text);
				DrawNumberBlock(drawList, rn, ImVec2(screenPos.x - sz.x * 0.5f, screenPos.y - sz.y), fontPx);
			}

			ImGui::End();
		}

		struct CreateD3DAndSwapChain
		{
			static void thunk()
			{
				func();

				const auto renderer = RE::BSGraphics::Renderer::GetSingleton();
				if (!renderer) {
					return;
				}
				const auto swapChain = reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain);
				if (!swapChain) {
					logger::error("couldn't find swapChain");
					return;
				}
				DXGI_SWAP_CHAIN_DESC desc{};
				if (FAILED(swapChain->GetDesc(std::addressof(desc)))) {
					logger::error("IDXGISwapChain::GetDesc failed.");
					return;
				}

				const auto device = reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder);
				const auto context = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);

				if (REL::Module::IsVR()) {
					// VR renders through the helper's world quads; keep the
					// device/context for RenderHud.
					g_d3dDevice = device;
					g_d3dContext = context;
					g_initialized.store(true);
					logger::info("D3D captured — VR world-quad rendering via ImGuiVRHelper.");
				} else {
					ImGui::CreateContext();
					auto& io = ImGui::GetIO();
					io.IniFilename = nullptr;

					if (!ImGui_ImplWin32_Init(desc.OutputWindow)) {
						logger::error("ImGui initialization failed (Win32).");
						return;
					}
					if (!ImGui_ImplDX11_Init(device, context)) {
						logger::error("ImGui initialization failed (DX11).");
						return;
					}
					Fonts::Load();
					g_initialized.store(true);
					logger::info("ImGui initialized (flat).");
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		// RE::HUDMenu::PostDisplay — per-frame draw entry point.
		struct PostDisplay
		{
			static void thunk(RE::IMenu* a_menu)
			{
				func(a_menu);

				if (!g_initialized.load()) {
					return;
				}

				// Drain the raw hook queue on the main thread — the hooks fire
				// on job/script threads and only capture POD.
				Capture::GetSingleton()->ProcessQueued();
				CombatLog::GetSingleton()->Tick();
				Capture::GetSingleton()->AuditTick();

				if (REL::Module::IsVR()) {
					if (!WorldQuadActive()) {
						return;
					}

					NumberManager::GetSingleton()->Update(g_resolved);
					if (g_resolved.empty()) {
						g_vrClient.SubmitWorldQuads(nullptr, 0);  // clear stale billboards
																  // Still run RenderHud so the panel clears its last pixels.
					}

					static const auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
					const ImVec2 displaySize{ static_cast<float>(screenSize.width), static_cast<float>(screenSize.height) };

					g_vrClient.RenderHud(g_d3dDevice, g_d3dContext, displaySize, []() {
						GImGui->NavWindowingTarget = nullptr;
						DrawWorldQuadFrame();
					});

					if (!g_resolved.empty()) {
						g_vrClient.SubmitWorldQuads(g_quads.data(), g_quads.size());
					}
				} else {
					NumberManager::GetSingleton()->Update(g_resolved);

					ImGui_ImplDX11_NewFrame();
					ImGui_ImplWin32_NewFrame();
					{
						// Render at the game's real resolution (upscaler-safe).
						static const auto screenSize = RE::BSGraphics::Renderer::GetScreenSize();
						auto& io = ImGui::GetIO();
						io.DisplaySize.x = static_cast<float>(screenSize.width);
						io.DisplaySize.y = static_cast<float>(screenSize.height);
					}
					ImGui::NewFrame();
					if (!g_resolved.empty()) {
						DrawFlatFrame();
					}
					DrawLiveDPSWindow();
					ImGui::EndFrame();
					ImGui::Render();
					ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
				}
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static inline std::size_t idx{ 0x6 };
		};
	}

	bool WorldQuadActive()
	{
		return REL::Module::IsVR() && g_vrClient.IsConnected() && g_vrClient.HasWorldQuads();
	}

	void Connect()
	{
		if (!REL::Module::IsVR()) {
			return;
		}
		if (!g_vrClient.Connect("FloatingDamageNG", FDNG_VERSION_STRING, ImGuiVRHelperPluginAPI::kClientFlag_WorldQuad)) {
			logger::warn("ImGuiVRHelper not found or registration failed — floating damage disabled in VR.");
			return;
		}
		logger::info("Connected to ImGuiVRHelper (world quads supported: {}).", g_vrClient.HasWorldQuads());

		// Load the damage font into the helper-owned HUD context when it is
		// created, so panel text rasterizes from real TTF outlines.
		g_vrClient.SetHudStyleCallback([]() { Fonts::Load(); });
	}

	void Install()
	{
		g_resolved.reserve(NumberManager::kPoolCapacity);
		g_quads.reserve(NumberManager::kPoolCapacity);

		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(75595, 77226), OFFSET(0x9, 0x275) };  // BSGraphics::InitD3D
		stl::write_thunk_call<CreateD3DAndSwapChain>(target.address());

		stl::write_vfunc<RE::HUDMenu, PostDisplay>();
		logger::info("Renderer hooks installed.");
	}
}
