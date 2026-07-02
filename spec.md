# Technical & Design Specification: VR Floating Damage & Combat Analytics System (VR-FDAS)

**Target Platform:** VR HMD Ecosystem  
**Rendering Interface:** `imgui-vr-helper` World-Space Quad API

---

## 1. Vision & Architecture Philosophy

This specification outlines a combined spatial visualization and real-time performance tracking framework designed exclusively for Virtual Reality. Traditional flat screen-space overlays cause stereoscopic convergence failure, depth mismatches, and severe eye strain in a Head-Mounted Display (HMD).

The VR-FDAS framework shifts all combat metrics into an optimized, world-anchored 3D spatial paradigm while introducing a decoupled analytics layer to capture behavioral combat events without breaking immersion.

                [ HMD Viewport ]
                       │
         ┌─────────────┴─────────────┐
         ▼                           ▼

[ 3D World Space ] [ Decoupled HUD ]
(Actor Floating Quads) (Player Status Plane)
├── Classic Float ├── Total Active DPS
├── Parabolic Pop (WoW Style) ├── Live Performance Bars
└── Radial Burst (AoE) └── Chromatic Threat Arcs

---

## 2. Spatial Visualizations & VR Layout Rules

All on-enemy textual UI elements must live naturally within the world geometry to preserve spatial presence, combat awareness, and visual comfort.

### Positioning & Depth Scaling

- **Bone Anchoring:** Quads must anchor their initial translation vectors to explicit 3D actor nodes (e.g., Target Head or Target Torso) via world-space coordinates.
- **Cylindrical Billboarding:** Text planes must rotate dynamically on the global Y-axis to face the player's HMD directional vector, ensuring legibility from steep vertical angles while avoiding unnatural roll/pitch warping.
- **Logarithmic Sizing Curve:** To prevent text from scaling down into illegible sub-pixels at long distances or clipping awkwardly through the camera up close, font sizes must evaluate against a non-linear distance curve:
  $$\text{FinalScale} = \text{Clamp}\left(\text{BaseScale} + \log_{10}(\text{Damage}) \times \text{Mod}, \text{MinScale}, \text{MaxScale}\right)$$

### Density Control & Anti-Stacking

- **Spiral Matrix Displacement:** Each time an actor takes damage, the system evaluates the number of active quads on that target and applies an outward-spiraling spatial offset vector ($\Delta x, \Delta y$). Consecutive hits branch outward symmetrically to avoid overlaps during high attack speeds.
- **Z-Depth Layering:** Introduce a micro-layering depth offset step on the camera view axis for newer quads to eliminate edge flicker and Z-fighting artifacts.
- **Relative Crowd Attenuation:** To maintain performance and visual breathing room during large-scale battles, a strict priority filter matrix dynamically strips weight from passive actors:

[ Combat Event Flagged ]
│
├──> Player Origin ──> Max Scale (1.0x - 1.5x), 100% Opacity, Focus Depth
│
├──> Follower Origin ──> Reduced Scale (0.70x), 60% Global Opacity Ceiling
│
└──> Enemy-on-Enemy ──> Minimal Scale (0.45x), 35% Opacity, Culled at long range

---

## 3. Engine Mapping: Damage Types & Actor States

The backend parsing architecture maps incoming combat hooks directly to targeted damage types, mitigation profiles, and special status flags.

### Core Color & Behavior Map

| Damage Category / State   | Visual Behavior Profile                                                | Recommended Hex Color        |
| :------------------------ | :--------------------------------------------------------------------- | :--------------------------- |
| **Physical (Standard)**   | Baseline impact profile. Scales with standard hit velocity.            | `#FF3B30` (Crimson)          |
| **Critical Hit**          | $1.5\times$ scale expansion, prolonged lifetime, rapid vertical surge. | `#FFCC00` (Amber Gold)       |
| **Blocked / Mitigated**   | Deflected horizontal trajectory drifting out to the sides.             | `#4A4A4A` (Dark Grey)        |
| **Fire Elemental**        | Spawns a tight, upward-flickering cluster mimicking embers.            | `#FF9500` (Blaze Orange)     |
| **Frost Elemental**       | Heavy, downward-sinking translation curve.                             | `#5AC8FA` (Ice Blue)         |
| **Shock Elemental**       | Erratic horizontal position jittering (electrical hum).                | `#AF52DE` (Lightning Purple) |
| **Poison / Decay**        | Small font footprint, slow drifting floating physics speed.            | `#4CD964` (Acid Green)       |
| **Healing / Restoration** | Inverse trajectory (smooth upward float).                              | `#34C759` (Bright Green)     |

### Advanced Combat Mechanics

- **Partial Mitigation (Shields & Resistances):** When a target successfully mitigates a strike, the layout splits the presentation: render the net damage dealt in its primary color, paired with a smaller, parenthetical secondary sub-text tracking the blocked or resisted amount.
  - _Visual Format:_ `104 (-45 Blocked)` or `62 (-20 Resisted)`
- **Multi-Source Component Splitting:** If an attack inflicts split physical and elemental payloads, the system processes them as discrete streams, spawning two quads simultaneously with a spatial offset (e.g., one crimson physical quad and one blaze orange fire quad).

---

## 4. The Player-Damage Paradox in VR

Displaying floating numbers tracking damage _dealt to the player_ directly out of their virtual body bones forces eye crossing, convergence failure, and fails completely when attacks hit from behind or from a flank outside the immediate peripheral cone.

### The Dual-Mode HUD Paradigm

When player-targeted tracking is toggled on, the system bypasses world-space body bone positioning and utilizes a decoupled visual pipeline:

1. **Static HUD Overlay Mode:** Projects player-targeted health/resource deltas onto a fixed, transparent UI overlay canvas suspended at a comfortable, head-locked convergence depth (~1.5 meters forward, anchored below the central gaze line).
2. **Directional Chromatic Arc System:** Spawns a curved, 3D directional crescent indicator around the player's center view vector. The arc color changes dynamically based on the damage attributes (e.g., flashing a lightning-purple arc on the right periphery if an incoming shock spell hits the player's right flank).

---

## 5. Combat Analytics & Logger Module (VR-CALM)

The tracking engine shifts the system from simple visual eye candy to a robust diagnostic workbench for build benchmarking, companion tracking, and simulation validation.

### Combat-State Engine Triggers

- **Session Start:** Initialized instantly when the player lands a stealth attack or draws aggressive threat from a hostile unit. The logger captures the initial high-precision timestamp and localized zone/cell identity string.
- **Session Close:** Evaluated when all actors in the player's active threat register are flagged dead, or when target lists clear entirely due to distance, routing, or active stealth mechanics.

### Metrics & Behavioral Analytics Tracking

| Metric                | Calculation / State                                             | Core Purpose                                                                              |
| :-------------------- | :-------------------------------------------------------------- | :---------------------------------------------------------------------------------------- |
| **Real DPS**          | $\text{Total Damage Output} / \text{Global Session Duration}$   | Tracks absolute damage efficiency across an entire encounter framework.                   |
| **Active DPS**        | $\text{Total Damage Output} / \text{Windows of Active Dealing}$ | Filters out transition times, such as chasing a flying or moving target.                  |
| **Time-to-Die (TTD)** | Time delta from an actor's first hit to zero health status      | Benchmarks weapon/build performance against specific enemy archetypes.                    |
| **Fled Combat State** | Detects threat clear when actor health is $> 0$                 | Tracks the performance and viability of crowd control, fear mechanics, or stealth breaks. |

---

## 6. Architectural Prompt for LLM Implementation

When providing this layout to an LLM to generate the implementation files, enforce the following technical parameters:

````markdown
# TARGET: Implement a high-performance VR Floating Damage & Analytics Engine using ImGui.

## 1. Execution Pipeline Constraints

- Use a pre-allocated vector object pool with a fixed constraint (e.g., 40 active world quads max) to manage active text instances. Enforce ZERO heap allocations during active combat tracking.
- Decouple all text position updates, alpha decays, and physics trajectories from the engine frame rate by scaling explicitly against `ImGui::GetIO().DeltaTime`.

## 2. Animation Profile State Machine

Implement a runtime switch supporting three configuration profiles:

- PROFILE_FLOAT: Linear vertical translation accompanied by an exponential alpha fadeout over the final 30% of the quad's lifespan.
- PROFILE_ARC: Initial explosive upward and outward velocity vector decaying into a smooth parabolic downward gravitational curve mimicking natural mass simulation (WoW Style).
- PROFILE_RADIAL: Outward expansion along a normalized radial circle centered on the impact node, optimized for area-of-effect spells or multi-target events.

## 3. Configuration Bindings Schema

Parse layout options using the following initialization format:

```ini
[CoreFilters]
bShowPlayerDamageDealt=true
bShowFollowerDamageDealt=true
bShowNPCOnNPCDamage=false
fMaxVisibilityRadius=20.0
iMaxConcurrentQuads=40

[DynamicSizing]
fBaseFontScale=1.0
fLogScaleModifier=0.25
fMaxFontScaleCeiling=1.6

[KinematicProfiles]
iSelectedProfile=1 ; 0 = Float, 1 = Arc, 2 = Radial
fGlobalSpeedMultiplier=1.1
fQuadLifetimeSeconds=1.5

[Analytics]
bEnableLiveDPSWindow=true
bLogFollowerPerformance=true
fPostCombatWindowFadeSeconds=5.0
bWriteLogToDisk=true
```
````
