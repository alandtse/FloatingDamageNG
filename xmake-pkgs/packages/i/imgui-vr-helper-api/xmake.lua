-- SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Modding-Exception
-- Client API for the ImGuiVRHelper SKSE plugin: four headers + one handshake
-- stub (compiled by the consumer — see src/ApiStubs.cpp). LGPL-3.0, designed
-- to be consumed source-form; pulled at build time instead of vendored.
package("imgui-vr-helper-api")
set_kind("library", { headeronly = true })
set_homepage("https://github.com/alandtse/imgui-vr-helper")
set_description("LGPL client API for ImGuiVRHelper (VR world-quad billboards for SKSE ImGui mods)")
set_license("LGPL-3.0-or-later")

add_urls("https://github.com/alandtse/imgui-vr-helper.git", { submodules = false })
add_versions("1.5.2", "eaecdd6a83ca7f2de8950835d87c0f9ce6840988")

on_install(function(package)
    os.cp("api/ImGuiVRHelperAPI.h", package:installdir("include"))
    os.cp("api/ImGuiVRHelperAPI.cpp", package:installdir("include"))
    os.cp("api/ImGuiVRHelperClientSDK.h", package:installdir("include"))
    os.cp("api/ImGuiVRHelperTypes.h", package:installdir("include"))
    os.cp("api/ImGuiVRHelperInput.h", package:installdir("include"))
    os.cp("api/COPYING.LESSER", package:installdir())
end)
