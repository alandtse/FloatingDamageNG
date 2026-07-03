Motion-effect presets. Each .json file here appears in the FloatingDamageNG
preset picker (SKSE Menu Framework > Floating Damage NG > Settings > Motion
effect > Preset), alongside the built-ins. Selecting one loads its path; you
can then tune the sliders and re-save.

Save a preset in-game: type a name in the "Preset name to save..." box and
click Save preset. Share a preset by copying its .json to another install.

Fields (see "Example - Cascade.json"):
  name            display name in the picker
  riseSpeed       upward velocity (game units/sec)
  riseAccel       vertical acceleration; negative = gravity (arc/fireworks fall)
  lateralSpeed    velocity along the launch direction
  lateralDamping  0 = constant speed; higher = bursts out then eases to a stop
  spreadPattern   0 alternate L/R, 1 rotate (fireworks), 2 diagonal alternate
  spawnAngle      degrees rotated per hit (rotate) or launch tilt (diagonal)
