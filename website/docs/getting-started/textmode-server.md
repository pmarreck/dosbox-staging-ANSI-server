# Text-Mode Frame Server

DOSBox Staging ships with an optional TCP service that streams 80×25
text-mode snapshots and accepts simulated keyboard input. The feature is
disabled by default; enable it from the configuration file to let external
tools scrape ANSI frames, push scripted keystrokes, or keep tabs on emulator
state without attaching a debugger.

## Configuration

```
[textmode_server]
# Core frame stream
enable = true            # default: false
port = 6200              # default: 6000
show_attributes = true   # emit ANSI colour when true, plain text when false
sentinel = 🖵            # UTF-8 marker separating metadata from payload
close_after_response = false  # close TCP socket after each reply when true
auth_token = ${DOSBOX_ANSI_AUTH_TOKEN}  # optional shared secret required by AUTH
debug_segment = 0x0000       # optional real-mode segment for DEBUG/PEEK shorthand
debug_offset = 0x0000        # offset added to segment<<4 (or use as physical when segment=0)
debug_length = 0             # bytes returned by DEBUG (0 disables the region)
```

Commands are newline-terminated and case-sensitive; verbs must be uppercase or
the server will reject them with `ERR commands are case-sensitive` while
suggesting the expected spelling. When the service is active the frame port
exposes the following protocol commands:

| Command            | Description |
|--------------------|-------------|
| `GET`              | Returns one snapshot (metadata + ANSI payload). |
| `GET SHOWSPC`      | Same as `GET`, but space characters are shown as middle dots. |
| `TYPE …`           | Sends key input to the guest. Add `GET` or `VIEW` at the end to fetch the resulting frame. |
| `STATS`            | Reports cumulative request, success, and failure counts. |
| `PEEK addr len`    | Reads `len` bytes from real-mode memory (physical or `segment:offset`) and returns uppercase hex. |
| `POKE addr hex`    | Writes hexadecimal bytes to real-mode memory (bounded by the server for safety). |
| `DEBUG`            | Returns `debug_length` bytes at the configured segment/offset as a hex dump. |
| `EXIT`             | Requests a clean emulator shutdown (`OK` is returned once accepted). |
| `AUTH token`       | Authenticates the session when an auth token is configured. |

### `TYPE` helper

`TYPE` accepts whitespace-separated, case-sensitive tokens and processes them
sequentially:

- Basic key names behave like `PRESS <key>` (for example `TYPE A B` presses the
  A and B keys).
- Suffix `Down` or `Up` to hold or release keys explicitly (`TYPE ShiftDown P
  ShiftUp`).
- Append `GET` or `VIEW` (synonyms) to request the post-input frame. Without
  either token the command replies with `OK`.
- Double-quoted strings expand into character-wise typing, automatically
  toggling `Shift` when needed (for example `TYPE "Peter" VIEW`). Use `\"`
  for literal quotes and `\\` for literal backslashes inside strings.
- Stand-alone tokens ending in `ms` introduce delays (for example `TYPE A 250ms
  B`). Values must be positive integers.
- Tokens ending in `frame` or `frames` defer execution for the requested number
  of presentation ticks (for example `TYPE A 3frames VIEW`).
- The literal backslash key is spelled `\\` outside of quoted strings.
- Unrecognised tokens are ignored after being logged to stderr.
- Use `Enter`/`Return` (or embed `\n` inside quotes) to submit an Enter key.

Examples:

```
TYPE ShiftDown P ShiftUp E T E R VIEW
TYPE "Peter" 150ms " Parker" VIEW
TYPE A B C
```

## Sample workflow

1. Launch the emulator with the server enabled (add the config block shown
   above or pass `-set textmode_server enable=true`).
2. Capture a frame:
   ```
   printf 'GET\n' | nc -N 127.0.0.1 6200
   ```
3. Type text and fetch the updated display:
   ```
   printf 'TYPE "hello"\n' | nc -N 127.0.0.1 6200
   sleep 0.1
   printf 'GET\n' | nc -N 127.0.0.1 6200
   ```
4. Shut down cleanly when finished:
   ```
   printf 'EXIT\n' | nc -N 127.0.0.1 6200
   ```

The frame metadata always precedes the payload and includes geometry,
cursor position (row, column, visibility), whether colour attributes were
included, and the comma-separated list of held keys (`keys_down`). Payload rows
use CP437 characters with 24-bit ANSI escape sequences when
`show_attributes=true` so the palette remains faithful to DOS. Plain text is
returned when `show_attributes=false`.

## Notes

- `EXIT` is the preferred way to stop headless sessions driven through the
  text-mode API. The emulator closes the listener once it acknowledges the
  request.
- The default sentinel is `U+1F5F5` (🖵). You can pick any UTF-8 sequence that
  does not appear in metadata/payload to simplify downstream parsing.
- `STATS` reports the cumulative counters plus a `keys_down` summary of keys
  that remain pressed after recent commands.
- `close_after_response=true` forces the server to close sockets after each
  reply; otherwise connections stay open and accept further commands.
- `TYPE` logs any token it cannot interpret and keeps processing the rest of
  the line; use stderr or the `STATS` counters to spot ignored input.
- `TYPE` executes via a queued action sink. `<N>frames` tokens and the
  `macro_interkey_frames` configuration option defer keystrokes until the
  emulator renders another frame, making inline `VIEW` snapshots more
  reliable.
- Authentication is optional. Set `auth_token` (or the
  `DOSBOX_ANSI_AUTH_TOKEN` environment variable) to require clients to start
  with `AUTH <token>`. A failed attempt closes the socket; success returns
  `Auth OK` and unlocks other verbs.
- `PEEK` accepts decimal or hexadecimal addresses (with optional `0x` prefix or `h` suffix) and supports
  `segment:offset` notation. Successful replies look like `address=0x0000FF00 data=DEADBEEF\n`.
- `POKE` expects an even number of hexadecimal digits (optionally prefixed with `0x`) and writes directly to
  real-mode memory. The write length is bounded internally to prevent runaway edits.
- Configure `debug_segment`, `debug_offset`, and `debug_length` when you need repeated dumps of a fixed region;
  the `DEBUG` command returns the same formatted line as `PEEK` without having to provide parameters.
