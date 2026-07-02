# FloatingDamageNG — Implementation Design

Refines [spec.md](spec.md) into a concrete, buildable architecture based on review of:
imgui-vr-helper (framework/template), FloatingSubtitles (world-quad client baseline),
KillFeed (hit-data capture; GPL-3.0 + modding exception, code reusable), the original
Floating Damage 0.4 (feature-set reference only — stat-delta polling, no code reuse),
and SkyrimCombatLogger (event-sink attribution reference only).

## Key refinements vs spec.md

1. **Damage amounts come from `Actor::HandleHealthDamage`, not the hit event.**
   The spec calls for intercepting "explicit damage source components from the combat
   pipeline". The concrete mechanism: a vfunc hook on
   `RE::Actor::HandleHealthDamage(Actor* a_attacker, float a_damage)`
   (vtable index `0x104` SE/AE, `0x106` VR — it is `SKYRIM_REL_VR_VIRTUAL`), installed
   on the `Character` and `PlayerCharacter` vtables. This fires for _every_ health
   damage application — melee, ranged, magic, enchantments, DoT ticks, traps, falls —
   with the true post-mitigation amount (`a_damage` is negative for damage). This is
   what the original Floating Damage could not do with stat-delta polling: no regen
   estimation heuristics needed, per-source amounts, works per tick.

2. **Hit metadata (crit/block/sneak, mitigation, physical component) comes from a
   `HitData` trampoline** at the engine's hit-dispatch site —
   `RELOCATION_ID(37633, 38586)` + `OFFSET(0x16A, 0xFA)`, `stl::write_thunk_call`
   (same site KillFeed hooks; VR shares SE IDs via CommonLibVR-NG address library).
   `RE::HitData` provides: `flags` (`kBlocked`, `kCritical`, `kSneakAttack`,
   `kPowerAttack`, `kBash`, `kFatal`), `totalDamage`, `physicalDamage`,
   `percentBlocked`, `resistedPhysicalDamage`, `resistedTypedDamage`,
   `criticalDamageMult`, `weapon`, `aggressor`, `target`. This powers the spec's
   `104 (-45 Blocked)` mitigation subtext and the physical-vs-enchant component split.
   The thunk signature differs by runtime (AE: `(AIProcess*, HitData&)`;
   SE/VR: `(ScriptEventSourceHolder*, NiPointer<TESObjectREFR>&, NiPointer<TESObjectREFR>&, FormID, FormID, HitData&)`).

3. **Correlation buffer instead of frame-countdown pairing.** FD 0.4 paired special-hit
   flags with stat deltas via a fragile 4-frame countdown. Here: the HitData thunk
   stores a frame-stamped `PendingHit` per victim FormID; when `HandleHealthDamage`
   fires (same frame or next), the amount is matched to the pending hit and consumes
   it. Unmatched damage (DoT ticks, traps) is classified via the victim's active
   effects using the **VR-safe** `MagicTarget::VisitEffects` visitor (never
   `GetActiveEffectList()` — CTDs on VR; KillFeed `CauseOfDeath.cpp:378-389` is the
   reference) plus `TESMagicEffectApplyEvent` (filter `kHostile|kDetrimental`).

4. **Element/type classification** by MGEF keyword (fire/frost/shock/poison per
   KillFeed's keyword tables) → spec's color map. Weapon hit with enchantment: the
   physical component renders crimson from `HitData.physicalDamage`; the enchant
   component arrives as its own `HandleHealthDamage` call attributed via the magic
   apply event → second quad with spatial offset (spec §2 component splitting).

5. **Kinematics computed in C++, not authored animation.** FD 0.4 delegated motion to
   Flash timelines. Here the three profiles (Float / Arc / Radial, spec §5) are
   closed-form functions of `age` seconds (dt-independent), evaluated per frame over a
   fixed-capacity object pool (`iMaxConcurrentQuads` slots, zero heap allocation in
   the combat path).

6. **Rendering = imgui-vr-helper world-quad client** (not a fork, not screen-space in
   VR). Connect with `kClientFlag_WorldQuad` (interface 004, helper ≥ 1.5); draw each
   damage number into a region of the helper-owned panel texture via
   `Client::RenderHud`, then `SubmitWorldQuads` with **raw Skyrim world coordinates**
   (the helper converts world→tracking at Submit time with a fresh pose — client-side
   conversion causes swim/jitter; see FloatingSubtitles `Renderer.cpp` comments).
   `kGameUnitToMeter = 0.01428f` for sizing `height_m`.

7. **Flat fallback**: same manager, different projection. Own ImGui context via
   `ImGui_ImplWin32/DX11` (device captured by a `BSGraphics::InitD3D` thunk,
   `RELOCATION_ID(75595, 77226)`), drawn from a `HUDMenu::PostDisplay` vfunc override
   (idx `0x6`), positions via `NiCamera::WorldPtToScreenPt3` with `g_worldToCamMatrix`
   (FloatingSubtitles `ImGui/Util.cpp::WorldToScreenLoc` is the reference). On VR
   without the helper installed, numbers are disabled (never screen-space in HMD).

8. **Player-received damage** (spec §4): phase 2. V1 ships world-anchored numbers for
   damage _dealt_; the head-locked peripheral plane / directional chromatic arc for
   incoming damage is a separate render mode added after the core loop is proven.

## Architecture

```
┌─ Capture (game thread) ─────────────────────────────────────┐
│ HandleHealthDamage vfunc (Character/PlayerCharacter)        │
│ HitData thunk @ RELOCATION_ID(37633,38586)                  │
│ TESMagicEffectApplyEvent sink                               │
│        → Correlator (per-victim PendingHit, frame-stamped)  │
│        → classified DamageEvent {victim, attackerKind,      │
│           amount, type, flags, mitigation, worldAnchor}     │
└──────────────── lock-free-ish mutex'd spawn queue ──────────┘
┌─ Model ──────────────────────────────────────────────────────┐
│ NumberPool: fixed array of NumberInstance                    │
│  {spawnPos, ageSec, profile, color, text[16], scale, alpha,  │
│   offsetSeed}  — spiral anti-stack offset per victim,        │
│   DoT accumulation (merge ticks < interval into one number), │
│   crowd attenuation (player/follower/NPC origin tiers)       │
└──────────────────────────────────────────────────────────────┘
┌─ Render (render thread) ─────────────────────────────────────┐
│ VR:  panel atlas layout → WorldQuad[] → SubmitWorldQuads     │
│ Flat: WorldPtToScreenPt3 → ImGui draw list at screen pos     │
└──────────────────────────────────────────────────────────────┘
```

Anchor: victim head node — `actor->GetMiddleHighProcess()->headNode->world.translate`
(fallback `GetPosition() + height`), captured at spawn time (numbers do not track the
actor afterwards; they animate from the impact snapshot).

## Repo layout / build (mirrors imgui-vr-helper)

- **xmake** (`xmake.lua` with `local version = "x.y.z"` line — required by
  `.releaserc` semantic-release replace), C++23, `mode.releasedbg`,
  `commonlibsse-ng.plugin` rule, deploy `after_build` via `SkyrimVRPluginTargets` /
  `SkyrimVRPath` env.
- **CommonLib**: submodule `lib/commonlibsse-ng` → `alandtse/CommonLibVR.git` branch
  `ng` (universal SE/AE/VR DLL, runtime-detected via `REL::Module::IsVR()`).
- **imgui-vr-helper API**: vendored `extern/imgui-vr-helper-api/` (the four LGPL
  `api/` headers + `ImGuiVRHelperAPI.cpp` — no xmake package exists yet).
- **Packages**: `imgui {dx11,win32}`, `toml++` (settings), `directxtk` (SimpleMath),
  `xmake-requires.lock` committed.
- **Scaffolding copied from imgui-vr-helper**: `.clang-format`,
  `.pre-commit-config.yaml`, `stylua.toml`, `.gitattributes`, `.gitignore`,
  `.github/workflows/` (build/lint/pr-lint/release/nexus-upload), `.releaserc`,
  `src/pch.h` pattern.
- **License**: GPL-3.0 with modding exception (compatible with copying from KillFeed).

```
src/
  main.cpp            SKSE entry, messaging (kPostPostLoad: hooks+VR connect; kDataLoaded: sinks)
  Hooks.{h,cpp}       HandleHealthDamage vfunc, HitData thunk, InitD3D thunk, HUDMenu::PostDisplay
  Capture.{h,cpp}     correlator, magic-effect sink, classification → DamageEvent
  NumberManager.{h,cpp} pool, spawn queue, kinematic profiles, accumulation, attenuation
  Renderer.{h,cpp}    VR world-quad client + flat ImGui path, panel atlas layout
  Settings.{h,cpp}    TOML/INI per spec §5 schema
  pch.h
```

## Milestones

- **M0** — repo scaffold builds; connects to helper (VR) / draws test number (flat).
- **M1** — HandleHealthDamage → numbers over victims, Float profile, physical/magic
  base colors, core INI filters.
- **M2** — HitData correlation: crit/block/sneak styling, mitigation subtext,
  component splitting, element colors.
- **M3** — Arc/Radial profiles, spiral anti-stacking, crowd attenuation, DoT
  accumulation window.
- **M4** — player incoming-damage modes (head-locked plane, directional arc), MCM/VR
  settings UI, VR live-DPS plane.

## Combat analytics (spec §5, implemented)

`CombatLog` opens a session when the player enters combat (TESCombatEvent /
`IsInCombat()` at first blood), tags wall-clock time + location, and accumulates
per-combatant damage dealt/taken, healing, hit/crit counts, time-to-die
(TESDeathEvent vs first-hit time), and survived/fled state. Real DPS = damage /
session duration; Active DPS uses the ACT/Details 3-second gap rule. Sessions
close when the player leaves combat (1 Hz poll) and append a report to
`<SKSE logs>/FloatingDamageNG-combat.log`; a small live DPS window (flat screen)
lingers `fPostCombatWindowFadeSeconds` after combat. Analytics record _before_
display filters, so the log captures NPC-on-NPC even when its numbers are hidden.

Healing capture: `ActorValueOwner::ModActorValue` vfunc 0x06 (`VTABLE[5]` — count
TESObjectREFR's secondary vtables) — positive `kDamage` delta on Health; per-actor
accumulation must clear `fMinHealToShow` within 1.5 s so regen trickle stays silent.
