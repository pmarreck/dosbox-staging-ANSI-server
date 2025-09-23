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

# Optional keyboard bridge
keyboard_enable = true   # default: false
keyboard_port = 6201     # default: 6001
```

When the service is active the frame port exposes the following protocol
commands:

| Command            | Description |
|--------------------|-------------|
| `GET`              | Returns one snapshot (metadata + ANSI payload). |
| `GET SHOWSPC`      | Same as `GET`, but space characters are shown as middle dots. |
| `APPLY …`          | Applies one or more keystrokes (see below) and, unless overridden, returns the resulting frame. |
| `STATS`            | Reports cumulative request, success, and failure counts. |
| `EXIT`             | Requests a clean emulator shutdown (`OK` is returned once accepted). |

### APPLY helper

`APPLY` accepts whitespace-separated tokens:

- `keys=RightRightA` – sequences are matched against the longest known key
  names (for example `Right`, `Left`, `Esc`) and one-letter tokens are treated
  case-insensitively.
- `keydown=Shift` / `keyup=Shift` – simulate modifier presses without automatic
  release.
- `response=ok|frame|none` – choose the reply type (`frame` is the default).

Examples:

```
APPLY keys=login response=frame
APPLY keydown=Shift keys=Tab keyup=Shift response=ok
APPLY keys=RightRightA response=none
```

### Keyboard port

If `keyboard_enable` is true a second listener handles low-level key commands:

| Command              | Description |
|----------------------|-------------|
| `PRESS <key>`        | Presses and releases a key (fails if the key is already down). |
| `DOWN <key>` / `UP <key>` | Explicit key down/up transitions. |
| `RESET`              | Releases every key currently held by the helper. |
| `STATS`              | Returns the command/success/failure counters. |

Key names mirror the emulator’s mapper: they include single letters and digits,
function keys (`F1`…`F24`), arrows, modifiers (`Shift`, `Ctrl`, `Alt`, `Gui`),
keypad entries (`NumPad1`, `KP+`, etc.), and many regional variants. The
matching is case-insensitive.

## Sample workflow

1. Launch the emulator with the server enabled (add the config block shown
   above or pass `-set textmode_server enable=true`).
2. Capture a frame:
   ```
   printf 'GET\n' | nc -N 127.0.0.1 6200
   ```
3. Type text and fetch the updated display:
   ```
   printf 'APPLY keys=hello\n' | nc -N 127.0.0.1 6200
   printf 'GET\n' | nc -N 127.0.0.1 6200
   ```
4. Shut down cleanly when finished:
   ```
   printf 'EXIT\n' | nc -N 127.0.0.1 6200
   ```

The frame metadata always precedes the payload and includes geometry,
cursor position (row, column, visibility), and whether colour attributes were
included. Payload rows use CP437 characters and ANSI escape sequences when
`show_attributes=true`.

## Notes

- `EXIT` is the preferred way to stop headless sessions driven through the
  text-mode API. The emulator closes both listeners once it acknowledges the
  request.
- `APPLY` is convenient for scripting, but the keyboard port remains available
  for tools that need precise control over modifier state or typematic timing.
- The default sentinel is `U+1F5F5` (🖵). You can pick any UTF-8 sequence that
  does not appear in metadata/payload to simplify downstream parsing.
