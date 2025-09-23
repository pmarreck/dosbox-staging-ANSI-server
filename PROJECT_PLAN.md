# DOSBox-Staging Text-Mode Server Plan

## Goal
Embed a first-class text-mode frame server into DOSBox-Staging so DOS-era applications (e.g., SAGA) can expose their 80×25 UI as ANSI or structured text over a localhost socket without ptrace/sudo tricks. The feature must deliver deterministic screen reads, cursor metadata, and remain optional/configurable inside the emulator.

## Background
- Current workflow relies on an external LuaJIT script (`vga_tap_ansi`) that reads VGA memory via `process_vm_readv`, requiring sudo and manual intervention.
- DOSBox-Staging already renders text-mode frames inside `src/hardware/video/vga_draw.cpp`; the new server can reuse that state instead of probing raw memory.
- SDL_net ships with DOSBox-Staging (used by IPX), so we can leverage it for a lightweight TCP listener.

## High-Level Requirements
1. **Configurable Service**: Add a `[textmode_server]` section with `enable`, `port`, `show_attributes`, and `sentinel` options. Disabled by default.
2. **Snapshot Engine**: Deterministically extract the current text plane, including character/attribute pairs, cursor info, blink status, and geometry.
3. **ANSI/Metadata Output**: Emit UTF-8 text preceded by various metadata "@meta" headers (which contain information such as the current cursor position, blink state, nanoseconds since epoch, etc.) followed by the body start sentinel (default `🖵`). Metadata lines follow the pattern `@meta key=value` and precede the `🖵` delimiter, which appears on its own line, after which the ANSI frame data is streamed. Preserve ANSI colors when enabled; provide plain text fallback via config.
4. **Non-Blocking Socket**: Run a simple protocol (e.g., `GET`, `GET SHOWSPC`, `STATS`) that returns one frame per request without stalling emulation.
5. **Testability**: Cover core logic with unit tests; only the SDL_net loop will require manual verification.

## Incremental Roadmap (TDD-Friendly)

### Phase 1: Configuration Skeleton
- Register `[textmode_server]` in config setup.
- Provide defaults: `enable=false`, `port=6000`, `show_attributes=true`, `sentinel="🖵"`.
- Unit tests: confirm defaults and overrides via the existing configuration test harness.

### Phase 2: Snapshot Core (Pure Data Extraction)
- Create `TextPlaneSnapshot` helper that, given mocked `VgaDraw`/`VgaConfig`, returns a matrix of `{char, attr}` and cursor data.
- Mirror logic from `VGA_DisplayStartLatch` and `VGA_TEXT_Draw_Line` (respects `vga.draw.blocks`, `address_line_total`, wrapping via `VGA_Text_Memwrap`).
- Tests: simulate 80×25 buffers (including wrap-around, blink bits) and assert grid + cursor results.

### Phase 3: Encoding & ANSI Formatting
- Implement CP437→UTF-8 conversion using helpers in `misc/unicode.cpp`.
- Port ANSI color mapping from the existing Lua script (`../regis-project/vga_tap_ansi_custom.lua`) so colour/intensity semantics stay consistent across tooling.
- Produce a string builder that combines metadata lines, sentinel markers, and ANSI payload.
- Tests: fixture-driven comparisons (e.g., known rows produce expected ANSI sequences, sentinel present, metadata parsed).

### Phase 4: Service Layer (Logic Only)
- Expose `TextModeService::GetFrame(const Options&)` that returns structured output when `vga.mode == M_TEXT`, otherwise an error string.
- Tests: verify behaviour across options (`show_attributes=false`, `include_cursor=true`), and out-of-mode handling.

### Phase 5: SDL_net Integration
- Introduce `TextModeServer` that creates a non-blocking listener during emulator init, polls in `DOSBOX_PollAndHandleEvents()`, and processes simple commands.
- Guarded by config flag; ensure clean shutdown in `QuitSDL` or section deinit.
- Manual validation: build release/debug, connect with `nc localhost 6000`, ensure single-frame response and metadata integrity.

### Phase 6: Documentation & Tooling
- Update README / website docs with usage, config snippet, and sample output.
- Provide a minimal CLI client script for regression tests (optional).

## Risks & Mitigations
- **Geometry edge cases**: leverage existing DOSBox calculations (`render_height`, `vga.draw.address_line_total`) and cover with tests.
- **Blink timing**: reuse `vga.draw.blink` state to avoid reimplementing timers.
- **Socket blocking**: use SDL_net with non-blocking `SDLNet_TCP_Accept`; limit work per poll to prevent frame hitches.
- **Upstream drift**: keep changes self-contained (new files + limited hooks in `sdlmain.cpp` / `vga_draw.cpp`) to ease rebases.

## Next Action
Implement Phase 1 (config skeleton + tests), then proceed sequentially, landing each phase as a separate changeset tracked by jj.
