// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <RE/Skyrim.h>
#include <REX/REX.h>
#include <SKSE/SKSE.h>

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <deque>
#include <filesystem>
#include <format>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

namespace logger = SKSE::log;
using namespace std::literals;

// Call-site offset within a RELOCATION_ID'd function; VR shares the SE offset.
#define OFFSET(se, ae) REL::VariantOffset(se, ae, se)

#define FDNG_STR_IMPL(x) #x
#define FDNG_STR(x) FDNG_STR_IMPL(x)
#define FDNG_VERSION_STRING      \
	FDNG_STR(FDNG_VERSION_MAJOR) \
	"." FDNG_STR(FDNG_VERSION_MINOR) "." FDNG_STR(FDNG_VERSION_PATCH)

namespace stl
{
	using namespace SKSE::stl;

	// Trampoline call-site thunk: struct with `static ... thunk(...)` and
	// `static inline REL::Relocation<decltype(thunk)> func`.
	template <class T, std::size_t Size = 5>
	void write_thunk_call(std::uintptr_t a_src)
	{
		auto& trampoline = SKSE::GetTrampoline();
		if constexpr (Size == 6) {
			T::func = *reinterpret_cast<std::uintptr_t*>(trampoline.write_call<6>(a_src, T::thunk));
		} else {
			T::func = trampoline.write_call<Size>(a_src, T::thunk);
		}
	}

	// Vfunc override at an explicit vtable + runtime-resolved index (for
	// SKYRIM_REL_VR_VIRTUAL functions whose slot differs between SE/AE and VR).
	template <class T>
	void write_vfunc(REL::VariantID a_vtable, std::size_t a_idx)
	{
		REL::Relocation<std::uintptr_t> vtbl{ a_vtable };
		T::func = vtbl.write_vfunc(a_idx, T::thunk);
	}

	// Vfunc override using the thunk's compile-time `idx` on `To`'s 0th vtable.
	template <class To, class T>
	void write_vfunc()
	{
		REL::Relocation<std::uintptr_t> vtbl{ To::VTABLE[0] };
		T::func = vtbl.write_vfunc(T::idx, T::thunk);
	}
}
