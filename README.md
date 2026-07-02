# FloatingDamageNG

Floating damage numbers for Skyrim — one DLL for **SE, AE, and VR**, with true
world-anchored 3D numbers in VR and a built-in combat analytics log.

**Download:** [Nexus Mods](https://www.nexusmods.com/games/skyrimspecialedition/mods/184159/) ·
**Source:** this repository (GPL-3.0 with modding exception)

## What you see

- Damage numbers over whoever takes the hit, colored by what caused it:
  physical (crimson), fire, frost, shock, poison, untyped magic, plus healing
  (green `+N`). An enchanted-weapon swing shows two numbers — the physical hit
  and the enchantment payload — because the engine applies them separately and
  so do we.
- **Crit / sneak / block** styling from the engine's real hit flags, and a
  mitigation subtext that says what actually happened: `104 (-45 armor)`,
  `(-30 blocked)`, or `(-20 resisted)` — sized by how much of the hit was
  mitigated. A fully immune target shows **RESISTED** in the element's color
  instead of nothing.
- **HEADSHOT** tags on projectile hits, read from the engine's impact skeleton
  node — works in vanilla, and stays accurate under locational-damage mods
  (whose multipliers are already reflected in the number; an implied `x2.5`
  shows when a hit exceeds its normal baseline).
- Numbers follow their target while loaded — a fleeing enemy under Flames
  carries its counting-up burn total with it; your self-heal rides along as
  you move. Damage-over-time ticks pool into one live-updating number instead
  of spam.
- Player-received damage and healing: over your head in third person, pinned
  to a configurable screen spot in first person (flat), anchored ~1 m ahead at
  chest height in VR.
- Three motion profiles: Float, Arc (parabolic), Radial burst. Big hits render
  bigger (log-scaled by damage). Crowd control for large fights: follower
  numbers smaller/dimmer, NPC-vs-NPC dimmer still and distance-culled.
- **Combat analytics** (optional): each fight is logged as a session — per
  combatant damage dealt/taken, healing, crits, time-to-die, who fled — with
  real and active DPS. A small live DPS readout shows during combat (flat),
  and an in-game stats browser with DPS graphs is available via SKSE Menu
  Framework. Sessions append to a plain-text log you can read outside the game.

## VR

In VR the numbers are **real 3D billboards at the target's world position**,
rendered through [ImGuiVRHelper](https://github.com/alandtse/imgui-vr-helper)'s
world-quad API with per-pixel depth occlusion. They are not drawn on the HUD
plane: flat screen-space overlays in a headset cause stereo convergence
mismatch (the number sits at HUD depth while the enemy is meters away), which
is uncomfortable at best. If ImGuiVRHelper is not installed, numbers stay off
in VR rather than falling back to a flat overlay.

## How it compares

All three mods put numbers over enemies. The differences are in how the damage
data is obtained and how the text is rendered. (Floating Damage details are
from reading its published v0.4 source; Modern Floating Damage from its public
description.)

|                      | **FloatingDamageNG**                                                                      | [Floating Damage](https://www.nexusmods.com/skyrimspecialedition/mods/14332)                           | [Modern Floating Damage](https://www.nexusmods.com/skyrimspecialedition/mods/170076)           |
| -------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------- |
| Runtimes             | SE + AE + VR, one DLL                                                                     | SE/AE                                                                                                  | SE/AE                                                                                          |
| VR                   | World-anchored 3D billboards                                                              | No (long-requested)                                                                                    | No (renderer is flat-screen)                                                                   |
| Damage data          | Engine damage + magic-effect pipeline hooks: true per-application amounts, source, caster | Polls Health/Magicka/Stamina deltas per frame; must estimate and subtract regen; no damage source info | Hooks the damage pipeline; splits physical vs enchant with per-source colors                   |
| Component splitting  | Yes (physical + each effect separately)                                                   | No (net stat change only)                                                                              | Yes                                                                                            |
| Mitigation breakdown | armor / blocked / resisted amounts, from live `HitData` + resist values                   | No                                                                                                     | Not advertised                                                                                 |
| Hit location         | HEADSHOT etc. from engine impact nodes, regex-configurable                                | No                                                                                                     | Not advertised                                                                                 |
| Renderer             | Dear ImGui, TTF fonts (D3D11; VR via ImGuiVRHelper)                                       | Scaleform SWF (Flash)                                                                                  | [PrismaUI](https://www.nexusmods.com/skyrimspecialedition/mods/170076) (embedded web/CEF view) |
| Combat analytics     | Sessions, DPS (real/active), TTD, fled, disk log, in-game graphs, optional MCP/REST       | No                                                                                                     | No                                                                                             |
| Extra dependencies   | None required on flat; ImGuiVRHelper for VR                                               | None                                                                                                   | PrismaUI                                                                                       |

If you play flat-screen only and want the web-styled visuals, Modern Floating
Damage is a good mod — the honest difference there is renderer taste
(embedded browser vs ImGui), analytics, and the mitigation/locational detail.
If you play VR, this is currently the only one of the three that renders in
world space at all.

## Requirements

- **SE/AE**: [SKSE64](https://skse.silverlock.org/), [Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- **VR**: SKSE VR, [VR Address Library](https://www.nexusmods.com/skyrimspecialedition/mods/58101),
  [ImGuiVRHelper](https://github.com/alandtse/imgui-vr-helper) ≥ 1.5
- Optional: [SKSE Menu Framework 3](https://github.com/alandtse/SKSE-Menu-Framework-3)
  for the in-game settings panel (color pickers, sliders) and combat-stats
  browser. Without it, everything is configurable in the INI.
- Optional: [devbench](https://github.com/alandtse/devbench) — when installed
  _and_ enabled (`bEnableDevBench`, off by default), combat stats are queryable
  over local MCP/REST for external tools.

## Configuration

`Data/SKSE/Plugins/FloatingDamageNG.ini` — every filter, color, threshold, and
motion setting, with comments. With SKSE Menu Framework installed the same
settings are editable in-game with live preview and saved back to the INI.
Combat session reports append to `FloatingDamageNG-combat.log` next to your
SKSE logs.

## Building

```powershell
git clone --recursive https://github.com/alandtse/FloatingDamageNG
cd FloatingDamageNG
xmake
```

Sister-repo client APIs (imgui-vr-helper, SKSE Menu Framework, devbench) are
pulled at build time from pinned upstream commits via the local package repo
in `xmake-pkgs/`. Set `SkyrimVRPath` or `SkyrimVRPluginTargets` to auto-deploy
after each build.

## Credits

- Display architecture based on [Floating Subtitles](https://github.com/powerof3/FloatingSubtitles)
  (powerof3; VR fork by alandtse).
- Hit-data capture patterns from [KillFeed](https://github.com/powerof3/KillFeed)
  (powerof3; GPL-3.0 with modding exception).
- The original [Floating Damage](https://www.nexusmods.com/skyrimspecialedition/mods/14332)
  by dougong defined the genre; this project reimplements the idea on a
  different capture and rendering architecture.

## License

GPL-3.0-or-later WITH LicenseRef-Modding-Exception — see [COPYING](COPYING) and
[EXCEPTIONS.md](EXCEPTIONS.md).
