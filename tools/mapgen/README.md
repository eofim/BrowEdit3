# rogen - Ragnarok map generator for BrowEdit3

Python (numpy only) library that writes `.rsw`, `.gnd`, `.gat` and `.rsw.extra.json` files which open in BrowEdit3.
Claude uses it to build maps from a description: terrain (noise, hills, rivers, lakes, plateaus, cliffs), textures,
water, paths, scattered models (trees, rocks), buildings, lights, sounds, effects and a GAT that matches.

## Workflow
1. In BrowEdit3: **File -> Export asset catalog (for Claude)**. This writes `catalog.json` next to BrowEdit3.exe,
   a list of every model and texture your client has. Claude searches it (`python catalog.py catalog.json tree`) and
   only uses names that exist.
2. Ask Claude for a map. It writes a short Python script (see `examples/village.py`), runs it and hands you the files.
3. Open the `.rsw` in BrowEdit3, then:
   * **Object Edit -> Smart GAT: recalculate all models** - exact GAT for every model (the generator only knows the
     rough footprint Claude gave it). After this, models keep following their GAT when you move or delete them.
   * Run the lightmapper (shadows), then save. Saving recalculates the quadtree.

## Coordinates
1 cube = 1 terrain square = 10 world units = 2x2 GAT cells. Positions are `(u, v)`, u west->east, v north->south,
`0..width` / `0..height`. Heights are world units, positive = up.

## Things to know
* GAT types used: 0 walkable, 1 blocked (models, deep water), 3 shallow water, 5 cliff.
* Cliffs: `terrace_rect()` raises/lowers whole cubes, the height step becomes a wall (`set_wall_texture`).
* Footprints (`footprint=(w, d)` in cubes) only exist to get a rough GAT; the recalculate command in BrowEdit makes it exact.
* Lightmaps start empty (no shadows) until BrowEdit3's lightmapper runs.

## Live mode (watch it being built)
BrowEdit3 watches the folder `claude_link\inbox` next to BrowEdit3.exe (toggle: File -> "Claude live link").
Drop a command file there and BrowEdit applies it to the open map a little each frame, with normal undo steps.
`MapGen.save_live(path)` writes a whole generated map as such a file (open a blank map of the same size first).
Results are written to `claude_link\outbox\<name>.result.json`, `claude_link\status.json` shows that BrowEdit is alive.

Commands: info, camera, textures, heights (+cliff walls), paint (row runs), gat (row runs), models (+smart_gat),
smart_gat, delete_models, water, save, undo, redo, wait. See `browedit/ClaudeLink.cpp`.
