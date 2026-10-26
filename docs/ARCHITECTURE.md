# OpenEDDI architecture

`editor::World` is a display projection, populated in memory by the pinned OpenBMH
reader. It does not save OBW. `legacy::Document` separately owns raw original master
bytes and validates every editable field. `legacy::Graphics` owns actual VGA files,
original palette indices and fixed slot bindings. Their `prepare` methods return
only changed full file bytes without touching the installation.

`editor::Session` coordinates commands, image conversion and a common undo history.
Both native widgets and MCP use this boundary. Before writing, the transaction
layer stages and validates prepared content, checks source fingerprints, backs up
full affected files and verifies hashes. It journals replacement and supports
recovery/restore under a process-level installation lock.

Four name pools stay append-only when references need new strings. Club/player
names use their reference tables; referee names use fifty u16 offsets in the
100-byte metadata table, stadiums use 168 u16 auxiliary offsets. The dependency
reader adaptations in `tools/patch_dependency.py` honor these references rather
than counting unreferenced pool strings as new entities. The patch is applied only
to OpenEDDI's pinned dependency, never another OpenBMH checkout.

Classic attributes keep their bit widths and unknown bits. Strength is derived.
Fixed squad capacities and the existing competition layout are not expanded.
Recognized VGA indexed graphics are quantized to their original slot sizes;
VGA RLE encoding preserves stream trailer and arbitrary suffix data. No writable
executable bitmap slots have yet been proven. The bounded embedded writer is
synthetically tested but is not exposed for arbitrary offsets.

No game simulation is started or changed by the editor.
