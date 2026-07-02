// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
// Copyright (c) 2026 FloatingDamageNG contributors. See COPYING and EXCEPTIONS.md.
//
// The imgui-vr-helper and devbench client APIs ship their SKSE-messaging
// handshake stubs as source (they must compile inside the consumer to share
// its CommonLib). The packages install them under include/; this TU builds
// them.

#include <ImGuiVRHelperAPI.cpp>

#include <DevBenchAPI.cpp>
