# SandDeformation (0.1.0, beta)

Runtime sand deformation for Unreal Engine: foot-shaped prints, landing craters with a raised rim, and a GPU heightfield that fades,
drifts with the wind and slides down steep slopes. The heightfield follows an actor, so the sand can be large. Any actor can ask for a mark,
not only characters.

Extracted from Project DUNE (Day 15-18). Only the **rendering** changes: the plugin never touches collision.

## Support

| | |
|---|---|
| Engine | Unreal Engine 5.8 (only version built and tested) |
| Platform | Windows / D3D12-SM5 or newer (compute shaders). Other platforms are not tested |
| Dependencies | the engine's `ProceduralMeshComponent` plugin (enabled automatically) |
| Modules | `SandDeformation` (Runtime, Default), `SandDeformationShaders` (Runtime, PostConfigInit: global shaders) |
| Shader path | `/Plugin/SandDeformation` -> `Shaders/` (mapped by the shader module) |

Without compute support (for example `-nullrhi`) the surface does not initialize and the sand stays flat; nothing crashes.

## Install

1. Copy the `SandDeformation` folder into `<YourProject>/Plugins/`.
2. Add it to the `.uproject` (`{ "Name": "SandDeformation", "Enabled": true }`) or enable it in *Edit > Plugins > Rendering*.
3. Open the project. The editor builds the plugin (C++ toolchain required, the plugin contains source).

**Packaging.** The plugin contains source, so a packaged game needs a C++ project (a content-only project is not compiled by the packaging tool and the plugin is then missing from the package). Tested: a clean C++ project (an empty game module) packaged with `RunUAT BuildCookRun` (Win64, Development, pak) and the packaged exe ran the self test (47 of 47 checks). The default material is held by the surface component's class default object so that it is cooked.

## Make a sand surface

1. Add a **SandSurfaceComponent** to any actor (or place a **SandSurfaceActor**).
2. Set `Patch Center` (where the patch starts), `Patch Size Cm`, `Tile Size Cm`, `Grid Cm`, `Heightfield Resolution`. Defaults: 1600 cm patch of 4x4 tiles, 2.5 cm vertex grid, 1024 texels (1.56 cm per texel, 2 x 16 MB).
3. **Ground height.** The patch vertices take their height from a line trace down onto `Ground Trace Channel` (default Visibility) around the component owner's Z (`Ground Search Cm`). The sand therefore lies on whatever has collision there: a static mesh, a procedural mesh, a Landscape. Tested: static mesh floor (self test) and a procedural analytic terrain (Project DUNE, through `GroundSampler`). **Landscape is not tested with the trace path** (Project DUNE's older levels use a Landscape through `GroundSampler`, with a floating patch).
4. **Hide the base surface under the patch.** The patch is drawn exactly on the ground, so a visible base mesh would hide every dent. Either hide your base mesh where the patch is (bind `On Tile Built`: it reports which world tile (a `TileSizeCm` square on the world grid) the patch just took over and which one it gave back; Project DUNE's terrain does this), or set `Ground Offset Cm` above 0 to let the patch float above the ground (the rim is blended down over `Edge Blend Cm`, baked: use it only with `Follow Target` off; deformation depth is then limited by `Max Depression Cm`, keep it below the offset).
5. **Material.** `Surface Material` is a material that reads the heightfield. Empty = the plugin's `M_SandDeformation_Default` (plain sand colour, no textures). To use your own sand material, give it these parameters (see the default material for the node setup): texture `TrailRT` (the heightfield), vector `TrailArea` (patch origin X, Y, size in cm), scalars `TrailRange` (height range, cm), `TrailDisplace`, vector `TrailShade` (1/resolution, normal strength, darkening per cm). Height is stored as `0.5 + h / TrailRange` in the red channel (0.5 = untouched sand); world position offset z = `(R - 0.5) * TrailRange * TrailDisplace`, with `uv = (WorldPos.xy - Area.xy) / Area.z`. The material is applied as a dynamic instance of your material.
6. **Follow.** `Follow Target` on: the patch moves with `SetFollowTarget(Actor)`, or with the local player's pawn (`Auto Follow Local Player`). Off: it stays.

## Make marks

**Blueprint / C++, from any actor, no character needed**

- `Apply Sand Deformation (World Position, Radius, Strength, Brush, Direction Yaw)` (function library): finds the surface under the position. `Radius` = half width of the dent in cm, `Strength` 1 = `Depth Per Strength Cm` (3 cm) deep, `Brush` Round (crater) or Foot (print; length about 2.4 x radius, `Direction Yaw` = toes). Returns false and does nothing when there is no surface, the position is outside the patch, or an input is NaN/infinite.
- `USandSurfaceComponent::ApplyDeformation(...)` the same on a given surface; `USandInteractionComponent::RequestDeformation(...)`.

```cpp
USandDeformationLibrary::ApplySandDeformation(this, GetActorLocation(), /*Radius*/ 40.f, /*Strength*/ 2.f, ESandBrush::Round);
```

**Characters.** Add a **SandInteractionComponent** to an `ACharacter` (or call `SetTrackedActor`). It stamps a foot print when a foot is planted (bones `foot_l/foot_r/ball_l/ball_r` by default, change `Foot Bones` / `Ball Bones`; with no bones a print every `Fallback Stride Cm`) and a crater on landing. Depth follows speed, `Weight Kg` and the slope. The floor under the character counts as sand when it is the owner of the surface component, has the tag `SandActorTag` ("SandSurface"), or `Any Floor Is Sand` is on.

Other Blueprint functions: `SetFollowTarget`, `SetSimulationEnabled`, `SetWind`, `ResetHeightfield`, `IsInsidePatch`, `GetPatchCenter`, `IsSurfaceReady`, `InitializeSurface` (when `Auto Initialize` is off).

## One interaction API for every actor (Day 20)

Everything that presses into the sand ends in **one request** and the **one shared GPU heightfield**:

`FSandInteractionRequest` -> `USandSurfaceComponent::ApplyInteraction` (also `ApplySandInteraction` in the function library) -> a stamp queue (world space, at most 16 per GPU pass, `Max Pending Stamps` 256, overflow is dropped and counted) -> one stamp pass -> the simulation -> the material.

| Request field | Meaning |
|---|---|
| `Brush` | `Footprint`, `Wheel`, `ObjectContact`, `Impact`, `Custom`: the pressure distribution (outline `Shape`: Auto, Round, Foot, Segment (a track from `PreviousPosition` to `WorldPosition`, round ends, no gaps between segments), Box (rounded rectangle, `Extent` = half length, half width, oriented by `MovementDirection` / `OrientationYawDeg`)) |
| `WorldPosition`, `Radius` | centre and half width (cm) |
| `Strength`, `Pressure` | depth = `Strength x Pressure x DepthPerStrengthCm` (3 cm) |
| `ImpactSpeed` | cm/s; the depth is multiplied by sqrt(`ImpactSpeed / ImpactReferenceSpeed`) |
| `DepthOverrideCm` | the depth in cm when you know it (limited by `MaxDepressionCm`) |
| `RidgeScale`, `Push` | raised rim (0 = none) and thrown-back sand for feet |
| `SourceActor`, `SurfaceNormal` | informational |

Convenience functions (Blueprint and C++): `ApplySandImpact`, `ApplySandPressure`, `ApplySandTrail`, `ApplySandInteraction`, `ApplySandDeformation`. A request is refused (false, counted: `InvalidRejected`, `OutsideRejected`) when the surface is not ready, a number is NaN, nothing would be pressed, or it is **outside the active patch**. Nothing outside the patch is stored (policy, same as for the scroll).

**Contact detection without calling the API every frame**

- `USandInteractionComponent` (characters): foot prints from the foot bones, landings. Heavy or large characters: `WeightKg`, `MaxPressureFactor`, `FootSizeScale`, `FootMin/MaxHalfLenCm`, `FootHalfWidthCm`.
- `USandContactPointComponent` (any actor): one per wheel, landing leg or resting body. It traces down once per `TraceIntervalSec` (0.033 s), not per frame, and only sends when something happens: a **landing** (impact, depth from the speed it came down with; also seen when the body bounced between two ticks), a **rolling wheel** (a track segment each `MinMoveCm`, nothing while it stands), a **resting pad** (one stamp, limited to `StaticDepthLimitCm`, again only after it has moved `MinStaticMoveCm`), a **dragged pad** (a trench). Load = `LoadKg` or the owner's mass divided by the contact points on it. `Mode Pad` with `Auto Extent From Owner` takes the footprint of the owner's root mesh (a crate needs no numbers). The speed is measured from the movement of the point, so physics bodies, scripted movement and attached components all work. Only impacts above `ImpactMinSpeedCm` count; a body that stays at rest never sends again.
- `ASandDemoActor` (crate with physics, four-wheel vehicle on a scripted arc, four-leg lander on a scripted descent) and `ASandDemoCharacter` (walks straight ahead) are test actors built from engine shapes or your own `BodyMesh` / skeletal mesh. They are not a driving or flight model.

Limits: one patch that follows one target (the local player by default): every actor that should leave marks must be inside it, so a level with several areas keeps them around the follow target (the test level does); a second patch for another area is not implemented. Contact points use a line trace to the Visibility channel under the point, so the sand needs collision there. Ellipse and custom-mask outlines are not implemented (a Box with rounded corners and Round are).

## Level test (needs a level with demo actors)

`-SandDeformationLevelTest` (rendered game, with `-SandDeformationSelfTestQuit`) finds the `ASandDemoActor`s / `ASandDemoCharacter` and the sand surface that are already in the level and checks them (impacts, tracks without gaps, pad craters, heavy foot prints, nothing sent at rest). `-SandDeformationOnly=none|crate|vehicle|lander|character` switches the others off (frame time comparison), `-SandDeformationNoShots` skips the screenshots.

## Level transitions and Landscape (Day 21)

- `-SandDeformationLevelHop=<map1>;<map2>;...` (rendered game, `;` separated because `FParse` stops at a comma, add `-SandDeformationSelfTestQuit`) opens the maps one after the other in one process. In each world: this world's surface is registered and ready, no surface of an older world is still registered, a dent request is accepted and read back, no non-finite height, no GPU failure. Checked: city sand level, sand test level, `L_DUNE_Prototype`, back to the first (4 hops).
- Optional surface properties for a mesh that lies ON a Landscape: `Material Lift Cm` / `Material Edge Blend Cm` set the material parameters `TrailLift` / `TrailEdge`. A material that has them lifts the mesh by `TrailLift` (blended to nothing over `TrailEdge` at the patch rim) so dents can go below the Landscape surface; a material without them ignores the values (the default material has none: it is lifted by `Ground Offset Cm` instead). `Ground Sample Stride` samples the ground every N-th grid point (default 4 with a ground sampler) to cut the tile build time.
- A mark made while the patch has not yet moved over the actor is not stored (see "Marks that leave the patch"). Driving back over your own track fills it again (the second pass pushes its rim into the first), which is why the Day 21 test drives forward only.

## Day 23: spheres, repeated landings, lifted sand

- **Sphere shape** (`Contact Shape` = Sphere): `Radius` is the radius of the sphere; the dent is the exact cap of that sphere (width follows from the depth, deepest in the middle, a rim around it). With `Previous Position` it is a groove with a round bottom. `USandContactPointComponent::bSphericalBody` (wheel mode, `WheelRadiusCm` = ball radius) uses it for landings and rolling; `Kind = Ball` of `ASandDemoActor` sets it. Cube / Box keep the flat-bottom rounded rectangle.
- **Accumulate** (`FSandInteractionRequest::bAccumulate`): the dent is added to the current depth and the added depth shrinks as the sand nears `Max Depression Cm` (soft saturation), so a repeated landing digs deeper. Used by landings: `ApplySandImpact`, the character landing of `USandInteractionComponent` and the touchdown impact of contact points. Footprints, tracks and pressure stamps stay absolute ("never deeper than this stamp"). Measured in PIE, 10 landings on one spot: player -5.1, -7.9, -9.4 ... -11.2 cm; golem -8.3, -10.5, -11.0 ... -11.2 cm (limit 11.2).
- **Lifted sand and characters**: with `Material Lift Cm` / `Ground Offset Cm` the sand is drawn above the ground the capsule stands on (the test level: 12 cm). `USandSurfaceComponent::GetRenderedLiftAt` gives that lift and `USandInteractionComponent::bAlignMeshToSurface` raises the character's skeletal mesh by it (visual only; capsule and collision untouched). It works for a character whose pose is not pulled back to the floor; **the third person mannequin's foot IK pulls the feet back to the collision floor, so the player's feet stay about 12 cm under the drawn sand until that foot IK is told about the lift** (`GetAppliedMeshLiftCm` is readable from an animation blueprint for that).
- `ASandDemoActor::SpawnDemo` spawns a Box / Ball with its start velocity applied (once the physics body exists).

## How the GPU heightfield works

Two RGBA32F render targets (ping-pong, UAV). Every pass reads one and writes the other, then they swap: **stamps** (up to 16 per pass: egg-shaped foot or round crater with ring, never deeper than the stamp, the rim only grows on unpressed sand), **simulation** every `Sim Interval` (0.05 s, at most `Max Sub Steps` per frame, only while something is left to settle): fade (`Recovery Seconds`, faster with wind), wind transport of the height deviation (upwind scheme), slope collapse above `Slope Threshold Deg` (conservative exchange), **scroll**. The rim of the texture absorbs sand. The game thread never reads the texture (readback functions `SampleHeightCm`, `ReadStats`, `SaveHeightfieldPNG` are for tests and stall the GPU).

## The moving patch: scope and limits

- The patch moves in whole tiles (`Tile Size Cm`, default 400) when the target is farther than `Scroll Threshold Cm` (400) from the patch centre. The heightfield is copied shifted on the GPU (one pass, no new resources): marks in the overlap stay exactly where they were, the newly exposed strip is untouched sand.
- **Marks that leave the patch are deleted.** Nothing is stored outside the patch; walking back finds flat sand.
- The mesh tiles that left are rebuilt on the other side, `Tiles Per Frame` (1) per tick. Until then the old tile is drawn undisplaced; a scroll is not repeated before the tiles are done. Each tile build samples the ground (a coarse trace grid, 1,700 traces per tile) and builds 26,000 vertices.
- Marks cut by the new rim are clipped there.
- `Heightfield Resolution` must be a multiple of the tiles per side (default 1024 / 4); otherwise the patch does not follow (warning in the log).
- One patch per surface; several surfaces may exist but a position uses the first one that contains it. No streaming of marks, no saving, no multiplayer, one local player.
- Mesh cost: the patch is about 415,000 vertices at the default grid (a dense procedural mesh, no Nanite / tessellation). Use a larger `Grid Cm` for a smaller cost.

## Not supported / not done

A vehicle model (the wheel tracks are made, the driving is yours), automatic detection of arbitrary collision shapes (contact points are placed by you), Ellipse / custom-mask outlines, Niagara dust, Nanite displacement, packaged-build and non-Windows testing, other engine versions, Landscape through the trace path, storing marks outside the patch, sand material beyond the default plain one (Project DUNE's textured sand material belongs to the game and is not part of this plugin).

## Self test

Launch a rendered game (not `-nullrhi`) on any map with `-SandDeformationSelfTest` (add `-SandDeformationSelfTestQuit` to exit when done). It spawns a floor, a surface and a plain sphere actor high above the origin (plus a sun and sky if the map has no directional light) and logs `SandSelfTest: PASS/FAIL ...` lines: material wiring, requests from the library and from a component on a plain actor, refused invalid input, the patch following the actor (overlap kept, new strip untouched, nothing at a shifted position), 200 random requests, reset. Screenshots go to `Saved/SandDeformationSelfTest/`.

## Licence note

This repository contains source code only: no materials, textures, meshes or other assets are included. The default material `M_SandDeformation_Default` referenced above is not part of this copy; build your own material with the parameters listed in the Material section.
