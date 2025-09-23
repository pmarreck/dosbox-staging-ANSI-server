# Agent Guidelines for DOSBox-Staging Text-Mode Server

This document guides AI agents contributing to the ANSI/text-mode server feature inside DOSBox-Staging.

## Project Overview

**Goal**: Integrate an optional text-mode frame server into DOSBox-Staging so DOS-era apps (e.g., SAGA) can stream 80×25 snapshots with ANSI formatting and cursor metadata over a localhost socket—no ptrace, no sudo.

**Repo Root**: `/home/pmarreck/Documents/dosbox-staging-ANSI-server/`

## Key Objectives
- Add a `[textmode_server]` config section (disabled by default) controlling `enable`, `port`, `show_attributes`, and a sentinel glyph (`🖵`).
- Build a snapshot helper that reads the current text plane via existing structures (`vga.draw`, `vga.config`, `VGA_Text_Memwrap`).
- Emit UTF-8 output with metadata lines (`🖵META key=value`), a `🖵PAYLOAD` delimiter, and an ANSI payload respecting foreground/background colors, blink, and cursor.
- Serve frames through a non-blocking SDL_net TCP loop handling commands like `GET`, `GET SHOWSPC`, and `STATS`.
- Cover pure logic (snapshot extraction, encoding, metadata) with unit tests before wiring SDL_net.

## Development Expectations
- Follow the incremental roadmap in `PROJECT_PLAN.md` (config → snapshot → ANSI → service → SDL_net → docs).
- Practice strict TDD on every code change: 1) write or extend a test and watch it fail, 2) implement the minimal code to turn the suite green, 3) clean up and only then move on. Capture new tests under `tests/` (gtest) and validate via `meson test -C build/debug`.
- Keep integration changes small: prefer new modules/functions to invasive edits.
- Maintain UTF-8 throughout—especially for the sentinel glyph; avoid lossy conversions.
- Document new options in README/website once stable.
- **Source control**: jj is co-located with git for this repo. Use jj commands (e.g., `jj undo`, `jj abandon`) to discard experiments without polluting git history.
- **Environment**: We are on NixOS. Use the provided `flake.nix` dev shell for a reproducible toolchain when hacking on C/C++ (enter via `nix develop`).
- Prefer American English spellings (e.g., "initialization") in new code, comments, and docs unless you are modifying an existing localized string.

## Testing & Validation
- Unit tests for each phase (config parsing, geometry math, ANSI formatting, metadata serialization).
- Manual smoke test of the socket server using `nc localhost <port>` after SDL_net integration.
- Optional: capture example outputs under `tests/data/` for regression comparison.

## Communication Notes
- Follow house tone: start responses with an exuberant quote or “Damn, it feels good to be a gangsta!”.
- When reporting progress, call out which roadmap phase is complete, in-progress, or blocked.

Stay focused on shipping the in-emulator text-mode server; defer unrelated DOSBox-Staging backlog items unless they unblock this feature.
