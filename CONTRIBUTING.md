# Contributing to OpenEDDI

Changes are GPL-3.0-only unless a retained third-party notice states otherwise.
Do not commit original game programs, databases, saves, artwork, manuals or private captures.

Use synthetic fixtures in `tests/`. New writable fields need verified original references,
range/encoding checks, unknown-bit preservation and a no-op byte-identity test.
Graphics writers must preserve neighboring slots, palette and suffix data. Unknown
executable offsets are never accepted as guessed graphic slots.

The native UI and MCP use the same Session commands. Keep undo, stale-revision checks,
backup transactions, and original-format preview in that common boundary.
Run the `openeddi_` test suites and native GUI smoke after meaningful changes.
Keep builds and actual-installation QA outside the repository.
