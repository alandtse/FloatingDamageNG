# FloatingDamageNG

World-anchored floating damage numbers for Skyrim VR (and flat SE/AE), rendered as
true 3D billboards via the [imgui-vr-helper](https://github.com/alandtse/imgui-vr-helper)
world-quad API — no screen-space overlay in the headset.

One universal SKSE DLL serves SE 1.5.97, AE 1.6.x, and VR 1.4.15
([CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng)).

## Features

- Real per-hit damage amounts from the engine's damage pipeline
  (`Actor::HandleHealthDamage`), not health-bar polling — magic, enchantments,
  DoT ticks, traps, and falls all report true post-mitigation values.
- Crit / sneak / block styling and a `(-N resisted)` mitigation subtext from
  live `HitData`.
- Elemental coloring (fire/frost/shock/poison) with physical-vs-enchant
  component splitting.
- Three kinematic profiles: Float, Arc (WoW-style parabola), Radial (AoE
  burst) — frame-rate independent, zero heap allocation in the combat path.
- Crowd attenuation: player hits full-size, follower hits smaller/dimmer,
  NPC-on-NPC dim and distance-culled.
- DoT accumulation: rapid same-type ticks merge into one running number.

See [spec.md](spec.md) for the design goals and [DESIGN.md](DESIGN.md) for the
implementation architecture.

## Requirements

- **VR**: SKSE VR, [VR Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/58101),
  and [ImGuiVRHelper](https://github.com/alandtse/imgui-vr-helper) ≥ 1.5
  (world-quad support). Without the helper, numbers are disabled in VR —
  a flat overlay in an HMD is an eye-strain bug, not a fallback.
- **SE/AE**: SKSE64 and [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444).

## Configuration

`Data/SKSE/Plugins/FloatingDamageNG.ini` — filters, sizing, kinematic profile,
colors. See the shipped file for the commented schema.

## Building

```powershell
git clone --recursive https://github.com/alandtse/FloatingDamageNG
cd FloatingDamageNG
xmake
```

Set `SkyrimVRPath` (install root) or `SkyrimVRPluginTargets` (semicolon-separated
Data/mod folders) to auto-deploy the DLL after each build.

## Credits

- Display architecture based on [Floating Subtitles](https://github.com/powerof3/FloatingSubtitles)
  (powerof3, VR fork by alandtse).
- Hit-data capture patterns from [KillFeed](https://github.com/powerof3/KillFeed)
  (powerof3; GPL-3.0 with modding exception).
- Feature-set homage to the original
  [Floating Damage](https://www.nexusmods.com/skyrimspecialedition/mods/14332) by dougong.

## License

GPL-3.0-or-later WITH LicenseRef-Modding-Exception — see [COPYING](COPYING) and
[EXCEPTIONS.md](EXCEPTIONS.md). The vendored `extern/imgui-vr-helper-api/` is
LGPL-3.0-or-later.
