Motion-effect presets. Each .json file here appears in the preset picker
(SKSE Menu Framework > Floating Damage NG > Numbers > Motion effect > Preset),
alongside the compiled default (Float). Selecting one loads its motion into
the live fields; you can then tune the sliders and re-save.

Float is the only compiled-in effect (the always-present default). Arc,
Radial, and Fireworks ship here as loose files so you can see and edit how
they are built - delete or change them freely; a reinstall restores them.

Freeze, Accelerate, Bound, and Drop approximate the classic Floating Damage
display modes (its mode 0 "Default" is our Float). They are our physics
take on those names, not exact copies - tweak to taste.

Save a preset in-game: type a name in the "Preset name to save..." box and
click Save preset (any name except "Float", which is reserved for the
built-in). Share a preset by copying its .json to another install.

Fields (see "Arc.json" / "Fireworks.json"):
  name            display name in the picker
  riseSpeed       upward velocity (game units/sec)
  riseAccel       vertical acceleration; negative = gravity (arc/fireworks fall)
  lateralSpeed    velocity along the launch direction
  lateralDamping  0 = constant speed; higher = bursts out then eases to a stop
  spreadPattern   0 alternate L/R, 1 rotate (fireworks), 2 diagonal alternate
  spawnAngle      degrees rotated per hit (rotate) or launch tilt (diagonal)
