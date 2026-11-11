# octree-cl — OpenCL sparse voxel octree renderer

Ray casts a sparse voxel octree (SVO) on the GPU with OpenCL, using the
**Laine & Karras ESVO traversal** ("Efficient Sparse Voxel Octrees", 2010) —
the standard fastest way to ray cast a pointer-based octree on a GPU.

## Build & run

Requirements: CMake ≥ 3.16, a C++17 compiler (Visual Studio 2019+ on Windows),
a GPU driver with OpenCL, and an internet connection on the first configure.

```sh
build.bat run              # Windows
./build.sh run             # Linux / macOS
```

This opens a window that renders in real time. You start as a player in the
current world (a flat platform by default; see *Scenes* below), with physics: walk, sprint and jump around (N switches to free fly). The first configure downloads
GLFW automatically; after that, everything builds offline.

### Controls

| Input | Action |
|---|---|
| Mouse | look around (the cursor is captured; the reticle marks the screen centre) |
| W A S D | walk |
| Space | jump (hold to keep hopping) |
| Shift | sprint |
| *in the air after a jump:* W / S (Z / S on AZERTY) | pitch 90° down / up |
| *in the air after a jump:* A / D (Q / D on AZERTY) | roll 90° left / right |
| *in the air after a jump:* Q / E (A / E on AZERTY) | yaw 90° left / right |
| *in the air, alternative:* O / L, K / M, I / P | pitch, roll, yaw (by key letter) |
| N | toggle walk ↔ free fly (noclip); walking starts where you are |
| *fly:* Space / E, Ctrl / Q | up, down |
| *fly:* Shift, mouse wheel | 4× faster, change fly speed |
| 1 2 3 4 | shaded / normals / traversal-step heat map / depth |
| F | toggle shadows |
| T | toggle colour filtering |
| G | texel grid per voxel face: 2×2 → 4×4 → 8×8 → 16×16 → smooth |
| F3 | toggle LOD (stop at pixel-sized voxels) |
| [ ] | halve / double the internal resolution |
| F4 | perspective ↔ fisheye lens |
| - / = | narrower / wider field of view |
| R | respawn (walk) / reset camera (fly) |
| Right click | place a block (hold and drag: a box, up to 32 per side); aiming at nothing places it in the air ahead |
| Left click | remove a block (hold and drag: a box) |
| F5 | edit mode (level editor) ↔ play mode (needs a start position) |
| Ctrl+S / Ctrl+O | save the world (all levels and the dream) to its file / reload it from the file (editor) |
| F6 | to the dream and back (editor) |
| PageUp / PageDown | grow / shrink (x2 per press, 1/16 to x4; editor and dream): blocks and reach follow your size |
| Tab | build menu: tool (blocks / portal / start / move), colour, grip on/off, block size |
| V | next tool: blocks → portal → start → move (→ object, when holding one) → none in levels; blocks → cage → reference (→ object) → none in the dream |
| *none tool* | no tool: clicks do nothing and nothing is highlighted (just look around and walk) |
| F11 (or Alt+Enter) | full screen on / off (`--fullscreen` starts in full screen) |
| *move tool:* left click, left drag on an arrow, right click | select an object, slide it along x / y / z, deselect |
| *cage tool (dream):* click a cage | go back to the level with a copy of the cage's model in hand (object tool) |
| *reference tool (dream):* click a cage | go back to the level with a **cage voxel** in hand: a live reference to the cage |
| *object tool:* right click | place what you hold (on the aimed face, or in the air ahead; in the dream: into a cage) |
| , / . | smaller / bigger blocks |
| Esc | release the mouse (click to capture again); Esc again quits |

The building keys (clicks, Tab, V, `,` `.`) and N (fly) work in edit mode
only. The window title shows the mode (EDIT / PLAY), messages such as "set a
start position first", the fps and the GPU render time per frame.

### Options

```
--world my.world      world file to load (if it exists) and to save to (default worlds/default.world)
--scene island        start a new world from another scene (default: flat)
--depth 9             octree depth: default 7 = 128^3 voxels; each +1 halves the voxel size
--size 1920x1080      window size
--scale 0.5           render at half resolution (for slower GPUs)
--lens fisheye        start with the fisheye lens (default: perspective)
--fov 90              field of view in degrees (perspective: vertical, default 70;
                      fisheye: horizontal, default 200, up to 360)
--vsync               cap to the display refresh rate
--fly                 start in free-fly mode
--keylog              print every key press to the console (diagnostics)
--no-filter           flat voxel colours
--texels 8            8×8 nearest-sampled texels per voxel face (default 4, 0 = smooth)
--device 1            pick another OpenCL device (all are listed at start)
--cpu                 run the same kernel as C++ on the CPU
--kernel my.cl        load kernel source from a file (iterate without rebuilding)
--bench               no window: time 30 frames and save render.bmp
```

If no OpenCL device is found it falls back to the CPU renderer at half resolution.

### Dependencies

| What | Why | How you get it |
|---|---|---|
| **GLFW 3.4** (zlib licence) | window, mouse and keyboard | downloaded by CMake (`FetchContent`); a system install is used if present |
| **OpenGL 1.1** | shows each frame as a full-screen texture | part of every GPU driver/OS; no loader library needed |
| **OpenCL** | runs the kernel on the GPU | GPU driver; loaded at runtime, so no SDK or headers are needed |

No other libraries are used. Build with `-DOCTREE_VIEWER=OFF` for a headless,
dependency-free binary.

## Scenes

Worlds live in `scenes/<name>/`, each with its source (signed distance +
colour rule), a README and a preview, and are registered in
`scenes/scenes.cpp`. `octree_render --help` lists them.

| Scene | |
|---|---|
| `flat` (default) | flat checkerboard platform, 1 voxel thick, with two blue grip walls |
| `island` | the original world, saved: terrain, striped sphere, gold ring |

To add one: copy `scenes/flat/flat.cpp`, rename the `Scene` constant, add it
to `scenes/scenes.cpp` and to `CMakeLists.txt`.

## Layout

```
kernels/octree.cl    traversal + shading kernel (embedded into the exe at build time)
src/main.cpp         options, backend selection, --bench mode, BMP output
src/viewer.cpp       GLFW window, input, walk/fly camera, frame display
src/physics.cpp      player sphere vs voxel collision, gravity, jumping
src/orientation.cpp  view orientation: mouse look, axis-aligned air turns, transitions
src/voxel_store.cpp  editable sparse octree (fill/clear boxes at any level)
src/world.cpp        World: editable store + GPU octree, Edit commands, history
src/build_tool.cpp   building: selection, targeting, drags, ghosts -> Edits
src/build_ui.cpp     build menu + HUD (OpenGL 1.1)
src/portals.cpp      portal doors: open/closed, mapping, physics rules, kernel params
src/portal_tool.cpp  portal tool: place / remove doors, ghosts
src/level.cpp        Level (world + portals + start position), LevelSet
src/start_tool.cpp   start tool: place / remove the start box, ghosts
src/move_tool.cpp    move tool: select connected built objects, gizmo, collision-checked moves
src/world_file.cpp   save / load a world (all levels + the dream) to one binary file
src/dream.cpp        the dream: indestructible ground, a grid of cages
src/object_tool.cpp  models (a cage's voxels, or a reference) and the object tool: placing them, ghost
src/renderer.cpp     camera maths, OpenCL backend, CPU backend
src/octree.cpp       scene (SDF + colours) -> compact breadth-first SVO
src/scene.hpp        scene interface
scenes/              the worlds (flat, island) and their registry
src/cl_loader.*      tiny dynamic loader for the OpenCL API
src/cl_shim.hpp      makes octree.cl compile as C++ for --cpu
cmake/embed.cmake    turns the .cl file into a byte array header
```

## How it works

**Data.** Each node is two `uint`s: index of its first child (children are
stored contiguously, only for existing slots) and an 8-bit *valid* mask plus an
8-bit *leaf* mask. A child's address is `first + popcount(valid & (bit-1))`.
A separate array holds one RGBA8 colour per node; interior nodes store the
average of their children, which is what the LOD cut-off displays. Nodes are
laid out breadth-first for cache locality. Fully solid regions become a single
coarse leaf instead of millions of voxels.

**Traversal (ESVO).** The octree occupies `[1,2]^3`, so every cube corner is an
exact float and the scale of a cube is a mantissa bit. The ray is mirrored so
it always travels toward −x,−y,−z, which makes child ordering and plane
selection branch-free. The loop does three things:

- **PUSH** into the child the ray is in, saving `(parent, t_max)` on a stack
  indexed by scale;
- **ADVANCE** to the next sibling by stepping over the nearest exit plane;
- **POP** when leaving the parent: the highest bit that differs between the old
  and new position *is* the scale to restart at, so it jumps straight to the
  right ancestor instead of unwinding level by level.

Traversal stops at a leaf, or when the voxel becomes smaller than
`--lod` pixels at its distance (which also makes far-away rays much cheaper).

**Lens.** The default is a regular pinhole perspective (frustum). F4 switches
to an *equidistant fisheye*: a pixel at distance `r`
from the image centre gets a ray at angle `r · fov/2` from the view axis
(`r = 1` at the left/right edges). Straight lines bend, and fields of view
above 180° work. Pixels that would look further than straight behind you
(only at very wide settings, in the corners) show a dark lens rim.

**Colour filtering.** Voxel colours are treated like a texture and filtered
bilinearly over the face that was hit: the hit voxel is blended with its three
nearest neighbours in the face plane (point queries down the octree to the
same level), skipping empty neighbours so colour never bleeds in from the air.
Geometry stays crisp; only the colour is smoothed. By default the blend is
sampled with *nearest texel* filtering: each voxel face is split into an N×N
grid (4×4) and every texel takes the blended colour at its centre, giving
pixel-art gradients instead of smooth ones. Cost: 3 short octree
lookups per pixel, roughly +10–25% frame time (T toggles it).

Shading is Lambert + sky ambient with a second octree ray for hard shadows.

## Player physics

The player is a **sphere** (radius 0.02 ≈ 2.5 default voxels) with the camera
rigidly at its centre, simulated on the CPU against the same octree the GPU
renders:

- **Collision.** Each substep asks the octree for the solid leaf boxes around
  the sphere (big solid blocks come back as one box, so this does not depend
  on depth). The closest point of a box is the centre clamped to the box, so
  contacts are exact.
- **Support vs walls.** If the contact point is *below* the centre (a floor, or
  the top edge of a ledge lower than the radius), the sphere is lifted straight
  up to rest exactly on it and stops falling. Walking into a low ledge
  therefore rolls you up and over it, smoothly and at any speed, and you never
  slide down slopes. Contacts at or above the centre are walls/ceilings: you
  are pushed out and slide along them. Anything taller than the radius needs a
  jump.
- **Motion.** On a surface, velocity eases toward the WASD target. **In the
  air** (after a jump or walking off an edge) nothing but gravity changes your
  velocity: no steering, you keep the momentum you had. Gravity 0.6 u/s², jump
  0.2 u/s (≈ 4 voxels), 0.1 s "coyote time" after leaving an edge, and a
  1-voxel snap so walking downhill doesn't hop.
- **Robustness.** Substeps of at most 1/240 s and never more than 30% of the
  radius, so fast falls cannot tunnel. Falling off the island respawns you, and
  a (re)spawn inside solid voxels pushes you up until free.

All tunables live in `PlayerConfig` in `src/physics.hpp`.

### Grip surfaces

Voxels can have a **grip** flag (`VOXEL_GRIP`, set by a scene's `grip()`
function; kept in `Octree::flags` on the CPU, the GPU never sees it). In the
`flat` world the blue walls have grip.

- A grip voxel only grips when it touches **the very bottom of your sphere**,
  relative to your rotation (the contact must be within ~3.6° of straight
  below along your body's up axis): you have to have rotated so your feet face
  the surface. Touched any other way (your side, the sphere meeting an edge),
  it is an ordinary wall or floor. When it does grip, you stick to it: world gravity switches off, you are held against the surface
  instead, and you walk on it like on the ground. Support, ledges and
  snapping all work along that surface's normal.
- **Leaving it** (jumping off it, or walking off its edge or onto a normal
  voxel) gives world gravity back, and you keep the velocity you built up on
  the surface. A jump pushes you away from the surface, along your up.
- **Coming out of a portal** that stands on a wall or ceiling, without having
  jumped, you grip that surface even if it is not grip voxels (see Portals).
  That grip lasts until you jump or walk off the surface.
- **Landing on normal ground while rotated** turns you upright again with the
  smallest rotation that brings your up back to world up, and your look
  direction turns with it: fall face down and you are looking straight ahead
  again. The camera eases into it like a turn (the screen still shows the
  ground for an instant, then swings up), and mouse movement during the
  easing simply adds on top.

To try it in `flat`: face wall 1, run at it, jump, press **back** (S: nose up: your
feet now point at the wall) and you land on it. On the wall, turn toward
wall 2 (they meet in an inner corner), run, jump, press **back** (S) again.

### Air rotation

After a jump, while you are off the ground, the movement keys rotate you by
90° per key press: **forward / back** pitch you down / up, **left / right**
roll you, and **Q / E** (A / E on AZERTY) turn you (yaw). They are matched by
position, so on AZERTY they are Z / Q / S / D. **O / L** (pitch), **K / M**
(roll) and **I / P** (yaw) do the same, by the letter printed on the key. In the air nothing
steers you: you keep your momentum.

1. **Snap.** Your view's front vector is replaced by its nearest world axis
   (e.g. (0, 0, 1) or (0, −1, 0)); then your right vector by its nearest axis
   perpendicular to that, which fixes the roll. You are now exactly
   axis-aligned.
2. **Turn** 90° around your right axis (forward/back), up axis (left/right)
   or front axis (roll keys). You are still axis-aligned afterwards.

Both happen instantly: that is your physical orientation. The camera, though,
eases from the old view to the new one over 0.25 s (`PlayerView::duration`
in `src/orientation.hpp`): the difference is kept as a world-space offset
rotation that decays to nothing (quaternion slerp with smoothstep), so mouse
look during the easing adds on top. No new turn can start until it finishes. The mouse keeps working throughout, relative to your
current body orientation, so you can end up walking around upside down or on
your side. Gravity and walking stay world-aligned; WASD walks toward wherever
"forward" and "right" point on screen, flattened onto the ground. R respawns
upright.

## Building

Aim with the crosshair (reach ≈ 45 blocks). **Right click** places the selected
block against the face you aim at, **left click** removes the block you aim
at. **Hold** either button and move: the ghost grows into a box between where
you pressed and where you aim now, and is built (or cleared) on release; at
most 32 cells per side. Pressing the other button during a drag cancels it.

**Tab** opens the build menu (the mouse is freed): 32 colours, a **grip**
toggle (placed blocks get the grip property, so you can build your own
walls to walk on), and the block size: the world's voxel size, or 1/2, 1/4,
1/8, 1/16 of it (also `,` and `.`). Removing with a small size carves small
holes out of big blocks; placing a big block next to a small one never
swallows the small one. Nothing is placed where it would intersect you.

**Building in the void.** When nothing is in reach, the block tool targets the
cell at a fixed distance along your view (`BuildTool::Config::voidDistance`,
0.06 ≈ 8 default blocks): the ghost floats there, outlined, and a right click
(or drag) places blocks in mid-air. Left click does nothing there (there is
nothing to remove). The portal and start tools still need a surface.

## Moving objects (move tool)

V until the tool icon shows four arrows (or the Tab menu).

- **Left click a built block**: the whole **object** it is part of is
  selected — every built block connected to it face to face, whatever their
  sizes. It is shown tinted, with a **gizmo**: three arrows from its centre,
  **x red, y green, z blue**, drawn on top of everything.
- **Left drag on an arrow** (aim at it, press, move the mouse): the view stays
  still and the object slides along that axis, following the mouse, in steps
  of its **largest voxel** (an object of only 1/2-size blocks moves by half
  blocks). It stops at the first step where it would overlap anything: other
  blocks or the scene, portal doors, the start box, you, or the world's edge.
  Objects never overlap.
- **Right click** deselects; so does switching tool or entering play mode.
- Only blocks you **built** move (they carry `VOXEL_BUILT`). The scene's own
  voxels (floor, walls, the island) and portal doors are not objects: clicking
  them says so. Touching blocks are one object, even if built separately.

**How it works.** `World::leavesIn` lists the editable tree's solid leaves in a
box; a flood fill over face-adjacent built leaves gives the object. The
selection is a voxel flag (`VOXEL_SELECTED`) that the serialiser turns into
colour alpha 0xFE, which the kernel tints — exact per voxel, and free. Before
each step, every moved leaf's target box is checked against the tree (any leaf
not selected = overlap), the locked regions and the player's sphere; the move
itself is ordinary Edits (remove, then place shifted with exact colours and
flags), so it is in `World::history()`. The gizmo is overlay boxes with the new
`OVERLAY_ONTOP` style (not hidden by the scene); grabbing an arrow is a ray
test against slightly fatter boxes.

**Ghosts** are drawn by the ray tracer itself (overlay boxes in the kernel
parameters), so they are depth-correct and work with both lenses: hover shows
the outline of the block a left click removes plus a faint ghost of the block a
right click places; a drag shows the whole box (tinted red for removal).

### How editing works (for extending it)

- `src/voxel_store.*` — the world as an **editable** sparse octree on the CPU.
  `fill(CellBox, Fill)` writes/clears any box of cells at any level: it splits
  big blocks where needed and merges 8 identical children back into one.
  `fromOctree` / `toOctree` convert to/from the compact GPU format (a round
  trip is byte-identical).
- `src/world.*` — `World` owns that store plus the serialised `Octree` used by
  the renderer and physics. Every change is an **`Edit` command**
  (place/remove, cell box, voxel, colour noise, keep-out sphere) applied by
  `World::apply` and recorded in `World::history()`, so undo/redo, saving a
  build, or sending edits over a network can be added on top. `commit()`
  re-serialises (≈1 ms on the flat world, ≈4 ms on the island) and the renderer
  re-uploads (`Renderer::updateOctree`).
- `src/build_tool.*` — selection, targeting (`pickVoxel` uses the same
  traversal code as the GPU), drags, bounded boxes, ghosts → `Edit`s. No
  window code, so other tools can reuse it.
- `src/build_ui.*` — menu + HUD drawn with OpenGL 1.1; one layout function
  for drawing and clicking.

## Portals

Two doors, each a rectangle of black voxels with a doorway on its entrance
side; from behind, a door is just a black wall. **One door is open at a time**:
its doorway shows (and leads to) what is in front of the other door. The
moment the camera goes through, the doors swap: the one you came through
closes, the one you came out of opens. You keep your speed and your view
turns with you, so the crossing is seamless.

**Any orientation.** A door stands on the voxel face you place it on: its "up"
is that face's normal, so a door can stand on the floor, stick out of a wall
or hang from a ceiling. Going through turns you the way the two doors are
turned relative to each other (your up becomes the exit door's up):

- You walk through while on the ground or gripping a wall (no jump) and the
  exit door stands on a wall or ceiling: you come out **gripping** that
  surface — any surface, not only blue grip voxels — and keep walking on it.
  The grip ends as usual: jump, or walk off the surface.
- The exit door stands on the floor: you come out normally on the ground.
- You jump (or fall) through: you come out airborne with normal gravity; if
  you are not upright, you are turned upright when you land, as after an air
  rotation.

**Portal tool** (V, or the tool row in the Tab menu):

- **Right drag** places a door: press on a voxel face where its bottom corner
  goes, drag to the opposite corner (sideways for the width, away from the
  face for the height). It stands on that face, in the plane perpendicular to
  it that faces you most, entrance toward you. The ghost shows the black wall and a
  coloured plate on the entrance side (blue for the first door, orange for the
  second).
- The **second door** always has the first one's size (its ghost shows it,
  bottom-centred where you aim): just click.
- **Left click** on a door removes it. Existing doors are outlined in their
  colour while the tool is active.
- Door walls are locked: the block tool cannot break them.

**How it works.** The doorway is an exact rectangle, not voxels. Each pixel's
ray is tested against the open doorway (from its entrance side only); the
octree is marched only up to it, and if the ray gets there first it is mapped
into the other door's frame (a 180° turn about the door's up, after lining the
two frames up) and marched again,
up to 4 times for doors that see each other. Fog and level of detail use the
total distance, so nothing pops at the doorway. Cost: one ray-rectangle test per
pixel (about 1% when no portal is on screen), plus the second march for the
pixels that look through. In the physics, the player can step into the open
door (its wall is ignored while in front of the doorway) and is teleported the
instant the sphere's centre — the camera — crosses it. Door axes are world
axes, so a crossing turns you by multiples of 90° and your body stays
axis-aligned (gravity, grip and air rotation keep working). The crossing
rotates the support direction too; if it no longer points up and you were
supported, `Player::gripAny` keeps you gripped to whatever surface is there. Ghosts are drawn on the first
segment only (not through a portal).

Code: `src/portals.*` (doors, mapping, physics rules, kernel parameters),
`src/portal_tool.*` (the tool and its ghosts), `World::lock` (protected walls),
and the portal loop in `render()` in `kernels/octree.cl`.

## The dream: cages and models

**F6** (in the editor) takes you to **the dream**, a separate world, and F6
again brings you back; each side remembers where you were. It is a pastel,
indestructible floor with a grid of 49 **cages**: boxes outlined by thin
white bars, a bit bigger than the player (8 default blocks a side). You
arrive in an aisle, facing one.

- **Build in the cages** with the block tool, exactly as in a level (all
  sizes, grip, void building). Building only works inside a cage: the floor
  and the bars can't be changed, and a drag stays in the cage it started in.
- The **cage tool** (V in the dream) handles cages. For now it does one
  thing: **click a cage** (it lights up under the reticle) and you are back
  in the level editor with its content **in hand** — the **object tool**.
- **Object tool** (in levels, once you hold something): **right click**
  places a copy, like a block: its side against the face you aim at,
  centred on the aimed point, or in the air ahead when nothing is in reach.
  The model is drawn see-through where the copy would go, and only where it
  fits — no overlap with blocks, the scene, doors, the start box or you. Place
  as many copies as you like; the copies are ordinary built blocks (the move
  tool moves them). V cycles through it with the other tools; taking another
  cage replaces what you hold.
- Copies follow your size: shrunk to 1/2, the copy is half as big.

**Your size.** PageUp / PageDown make you twice bigger / smaller (from 1/16
to 4 times the normal size), in the editor and in the dream. Your speed,
jump, reach and the size of the blocks you place all follow (the block size
setting stays relative to you): shrink to build fine details, grow to block
out big shapes fast. You can't grow where there is no room. Play mode is
always at normal size.

**How it works.** The dream is a `Dream` (`src/dream.*`): a `Level` (its own
`World`, no portals) plus the list of cage interiors; each interior is
exactly one octree cell of level 4 (1/16 of the world), so a cage's content
is a sub-tree. `extractModel` copies the voxels inside a cage (relative to
the cage) and builds a small octree of them. The renderer gets that octree as
a **ghost** (`Renderer::setGhost`): the kernel marches it in its own space
(`p_ghost = 1 + (p - origin) / size`, same `t`) and blends it over the scene
where it is in front — exact, at any position and power-of-two scale. Copies
are placed on a grid of the model's largest voxel; the fit test checks every
voxel against the world, the locks and the player. Building in the dream is
the block tool restricted to the cages (`BuildTool::setAllowed`). The world
file (version 2 and up) stores the dream with the levels; version 1 files still
load (with a new, empty dream).

## Cage voxels: references to a cage

The **reference tool** (V in the dream, after the cage tool; its cage
highlight is blue) takes a **cage voxel** instead of a copy. A cage voxel is a
single voxel that *is* the cage's content, at the voxel's size:

- It is placed like any held object (right click), but always as **one
  octree node**: its cube sits exactly on the grid of its own size. At normal
  size that is a cube of 8 blocks on an 8-block grid; shrink (PageDown) to
  place smaller ones on finer grids (1/2 size: 4-block cubes, ...). It needs
  its whole cube free, and it owns its whole cube, empty parts included.
- It is **live**: change the cage in the dream and every cage voxel of it
  changes, everywhere (you see it when you come back from the dream).
- Its content **can't be changed from the level**: blocks are not placed
  inside its cube, painting over it does nothing, and removing any part of it
  (left click with the block tool) removes the whole cage voxel. Move it with
  the move tool (it moves by its own size).
- **Nesting**: hold a cage voxel (or a copy) and press F6: in the dream you
  can put it in another cage. A cage can never contain itself, directly or
  through other cage voxels — the placement is refused ("a cage can't contain
  itself"). A copy of a cage that holds cage voxels keeps them as cage voxels.
- An empty cage can't be taken.

Note on the flat level: its floor top is at row 33, not on the 8-block grid,
so a normal-size cage voxel can't stand directly on it (it sits on the next
grid step, 7 blocks up); half or quarter size get closer, and an aligned floor
(like the dream's) is exact.

**How it works.** A cage voxel is a leaf with the flag `VOXEL_INSTANCE`
whose colour field holds the cage's index. The editable tree treats it as one
unit (never split, merged or painted over). When the tree is written for the
GPU and physics, each referenced cage's content is written once into the same
buffer, and every cage voxel becomes a copy of that content's root node — so
all of them share the same children: the octree becomes a **DAG**. The ray
marcher needs no change for that (a node only says where its children are;
positions come from the path), except a guard for very deep nesting; physics
and picking walk it the same way. Nested cages resolve recursively, with a
guard that cuts any cycle. A second cage voxel of the same cage costs one GPU
node. Coming back from the dream re-resolves the level. World files (version
3) store cage voxels as references (the raw tree), not their content.

## World files (saving and loading)

A **world** is the set of levels; it is saved as one file.

- **Ctrl+S** (in the editor or the dream) writes every level and the dream to the world file: its voxels
  (built blocks keep their colour, grip and "built" marks, so they can still be
  moved), its portal doors (and which one is open) and its start position.
  The file is written beside the old one and then renamed over it, so a failed
  save never destroys your last good file.
- **Ctrl+O** reloads the world file, replacing what is in memory (unsaved
  changes are lost). A damaged or foreign file is refused with a message in
  the title bar, and nothing changes.
- **At start**, the world file is loaded if it exists. It is
  `worlds/default.world` in the project folder unless you pass
  `--world FILE`. Without a file (or with `--scene NAME` and no `--world`),
  you start a new world from the scene, and Ctrl+S creates the file.
- Not saved: the edit history and the move tool's selection.

The format is documented at the top of `src/world_file.hpp` (a header with a
version number, then each level: name, octree, doors, start). A world made
of the 128³ island and a flat level is about 2 MB.

## Levels, edit mode and play mode

What you build in is the **level editor** (edit mode). **F5** switches to
**play mode**: no tools, no build menu, no ghosts and no free fly — just the
player, starting at the level's start position. F5 again goes back to the
editor, where you were. Play mode is only possible once the level has a start
position; otherwise F5 tells you so in the title bar.

**Start tool** (V until the tool icon shows a green box, or the Tab menu):

- **Right click on a floor** puts the start box there: a box of the player's
  size standing on the aimed cell, facing the way you look (snapped to 90°;
  the plate on one side shows which way). There is one start position, so
  placing again moves it. The ghost turns red where it can't go (not a floor,
  voxels or a door in the way).
- **Left click on the start box** removes it.
- The start box keeps its space clear: blocks are not placed inside it and
  doors can't go through it. It is drawn in the editor only; in play mode it
  is invisible (and never solid).
- Respawning (R, or falling off the world) puts you back at the start,
  facing its direction, in play mode — and in the editor when it is set.

**Code.** A `Level` (`src/level.*`) owns everything a playable map has: its own
editable `World` (octree), its `Portals` and its `StartPoint` (the box is a
locked region of the world, `World::lock`, which is what keeps blocks and
doors out). A `LevelSet` holds the game's levels; `main` creates one level
from the scene, and the viewer edits and plays `levels.current()`. Switching
levels later is `bindLevel()` in `viewer.cpp` (points the renderer and the
physics at another level's octree). Still to do for several levels: a way to
add levels in the editor, and to pick or chain them (e.g. a goal object), and per-level
play state (doors, collected things) reset when play starts.

## Where to go next

- **Skip the readback:** each frame is copied GPU→CPU→GL texture (~1 ms at
  1080p). `cl_khr_gl_sharing` lets the kernel write straight into the GL
  texture.
- **Beam optimisation** (ESVO §5.2): cast one coarse ray per 8×8 tile first
  and start the per-pixel rays at its hit distance — typically 1.5–2× faster.
- **Sparse voxel DAGs** (Kämpe et al. 2013) to merge identical subtrees and fit
  much deeper scenes in VRAM; the same traversal works with minor changes.
- Load real data (e.g. `.vox` files or a mesh voxeliser) in place of the SDF
  scene in `src/octree.cpp`.
