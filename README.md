# DOSBox Staging

![GPL-2.0-or-later][gpl-badge]
[![Chat][discord-badge]][discord]

DOSBox Staging is a modern continuation of DOSBox with advanced features and current development practices.

It is a (mostly) drop-in replacement for older DOSBox versions—your existing configurations will continue to work, and you will have access to many advanced features.

For a detailed description of the project's scope please refer to the [About](https://www.dosbox-staging.org/about/) page on our website.

## Donations

If you enjoy using DOSBox Staging, please [consider a
donation](https://www.dosbox-staging.org/get-involved/#make-a-donation) to the
project.

If you want to help but can't afford a donation, check out the [Get
involved](https://www.dosbox-staging.org/get-involved/) page of our website
for other ways to contribute.


## Project website

https://www.dosbox-staging.org/

First-time users and people migrating from other DOSBox variants should start by reading the [Getting started guide](https://www.dosbox-staging.org/getting-started/).

## Text-mode server

The optional text-mode server lets automation harnesses stream ANSI snapshots
of the 80×25 text plane, capture cursor metadata, and drive simulated keyboard
input over a localhost TCP socket—no debugger or ptrace hooks required. It is
disabled by default; enable it in the configuration file whenever you need a
headless interface.

### Configuration

```
[textmode_server]
enable = true             # start the service (default: false)
port = 6000               # listener port (uint16)
show_attributes = true    # emit 24-bit ANSI colour or plain text
sentinel = 🖵             # UTF-8 delimiter before the payload (defaults to 🖵)
close_after_response = false  # close sockets immediately after replies

# TYPE macro behaviour
macro_interkey_frames = 1      # frame delay inserted between expanded characters
queue_non_frame_commands = true  # queue TYPE even without GET/VIEW
allow_deferred_frames = true     # allow deferred GET/VIEW replies
```

The service generates UTF-8 metadata lines prefixed by the sentinel (columns,
rows, cursor, colour mode, keys currently held) followed by a standalone
`sentinel + "PAYLOAD"` line and the ANSI frame. Colours are emitted as
true-colour SGR sequences that match the original DOS palette. When
`show_attributes=false`, plain CP437 characters are returned instead.

### Protocol

Commands are newline-terminated, case-sensitive verbs. The verbs themselves
must be uppercase; the server suggests the correct spelling and replies with
`ERR commands are case-sensitive` if it receives `get`, `type`, and so on.

| Command       | Description |
|---------------|-------------|
| `GET`         | Emit one snapshot (metadata + ANSI payload). |
| `GET SHOWSPC` | Same as `GET`, but spaces render as middle dots. |
| `TYPE …`      | Enqueue keyboard events, delays, and optional frame capture. |
| `VIEW`        | Synonym for `GET` (allowed as a trailing token inside `TYPE`). |
| `STATS`       | Report cumulative request/success/failure counters plus `keys_down`. |
| `EXIT`        | Request a graceful emulator shutdown. |

### `TYPE` tokens

`TYPE` token parsing is intentionally strict to keep scripts reproducible:

- Tokens are whitespace-separated and case-sensitive. Invalid casing is logged
  to stderr alongside the expected spelling.
- Bare key names perform a `PRESS` (down + up). Append `Down` or `Up` to
  hold/release keys manually (`TYPE ShiftDown P ShiftUp`).
- Double-quoted strings expand to character-wise typing with automatic
  `Shift` insertion (for example `TYPE "Peter" VIEW`). Escape quotes with
  `\"` and backslashes with `\\`.
- Delay tokens ending in `ms` apply millisecond waits. Tokens ending in
  `frame` or `frames` wait for presentation ticks processed by the queued
  scheduler (`TYPE A 3frames B`).
- Trailing `GET` or `VIEW` requests the post-input frame. Without either token
  the command replies with `OK`.
- Literal Enter can be sent as `Enter`, `Return`, or via `\n` inside quotes.
  The literal backslash key is expressed as `\\` when unquoted.
- Unrecognised tokens are ignored after a stderr warning so scripts keep
  running.

### Future enhancements

Work is underway to route every `TYPE` command through an asynchronous queue so
long macros no longer block the emulation thread before a trailing `VIEW`. The
queue already powers `<N>frames` delays and inter-key waits; upcoming work will
flush queued actions from the main poll loop and tighten the round-trip tests.

### Known issues

- On some hosts Git reports spurious modifications to
  `extras/windows-installer/.gitattributes` and
  `extras/windows-installer/dosbox_with_console.bat` due to CRLF conversions.
  The files are byte-identical to HEAD; review Git's autocrlf settings if the
  noise becomes distracting.

Additional examples and client snippets live in the
[text-mode server guide](https://www.dosbox-staging.org/getting-started/textmode-server/).

## Build status

[![Linux x86\_64 build status][build-lin1-badge]][build-lin1-ci]
[![Linux other build status][build-lin2-badge]][build-lin2-ci]
[![Windows (VisualStudio) build status][build-win-msvc-badge]][build-win-msvc-ci]
[![Windows (MSYS2) build status][build-win-msys2-badge]][build-win-msys2-ci]
[![macOS build status][build-mac-badge]][build-mac-ci]


## Stable release builds

[Windows](https://www.dosbox-staging.org/releases/windows/),
[macOS](https://www.dosbox-staging.org/releases/macos/)
[Linux](https://www.dosbox-staging.org/releases/linux/),


## Test builds & development snapshots

[Development builds]

## Key features for developers

| **Feature**                | **Status**                          |
| -------------------------- | -------------------------------     |
| **Version control**        | Git                                 |
| **Language**               | C++20                               |
| **Logging**                | Loguru for C++<sup>[3]</sup>        |
| **Build system**           | CMake + Ninja or Visual Studio 2022 |
| **Dependency manager**     | vcpkg                               |
| **CI**                     | Yes                                 |
| **Static analysis**        | Yes<sup>[1],[2]</sup>               |
| **Dynamic analysis**       | Yes                                 |
| **clang-format**           | Yes                                 |
| **[Development builds]**   | Yes                                 |
| **Unit tests**             | Yes<sup>[4]</sup>                   |

[1]: https://github.com/dosbox-staging/dosbox-staging/actions/workflows/clang-analysis.yml
[2]: https://github.com/dosbox-staging/dosbox-staging/actions/workflows/pvs-studio.yml
[3]: https://github.com/emilk/loguru
[4]: https://github.com/dosbox-staging/dosbox-staging/tree/main/tests
[Development builds]: https://www.dosbox-staging.org/releases/development-builds/

## Source code analysis tools

- [PVS-Studio](https://pvs-studio.com/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) — C++ static analyser
- [Pylint](https://pypi.org/project/pylint/) — Python static analyser
- [markdownlint](https://github.com/DavidAnson/markdownlint) — style checker and linter for Markdown
- [ShellCheck](https://www.shellcheck.net/) — shell script analysis tool

## Dependencies

DOSBox Staging has the following library dependencies:

| Dependency                                               | Provides feature                                | vcpkg package name | vcpkg version   | Optional?           |
| -------------------------------------------------------- | ----------------------------------------------- | ------------------ | --------------- | ------------------- |
| [Google Test+Mock](https://github.com/google/googletest) | Unit testing (development)                      | gmock              | 1.6.0#1         | yes :green_circle:  |
| [IIR](https://github.com/berndporr/iir1)                 | Audio filtering                                 | iir1               | 1.9.5#1         | **no** :red_circle: |
| [libpng](http://www.libpng.org/pub/png/libpng.html)      | PNG encoding of screen captures                 | libpng             | 1.6.46#0        | yes :green_circle:  |
| [Munt](https://github.com/munt/munt)                     | Roland MT-32 and CM-32L emulation               | libmt32emu         | 2.7.1#0         | yes :green_circle:  |
| [Opus File](https://opus-codec.org/)                     | CD Audio playback for Opus-encoded audio tracks | opusfile           | 0.12+20221121#1 | **no** :red_circle: |
| [SDL 2](https://github.com/libsdl-org/SDL)               | OS-agnostic API for video, audio, and eventing  | sdl2               | 2.32.6#0        | **no** :red_circle: |
| [SDL_net 2](https://github.com/libsdl-org/SDL_net)       | Network API for emulated serial and IPX         | sdl2-net           | 2.2.0#3         | yes :green_circle:  |
| [SpeexDSP](https://github.com/xiph/speexdsp)             | Audio resampling                                | speexdsp           | 1.2.1#1         | **no** :red_circle: |
| [Tracy Profiler](https://github.com/wolfpld/tracy)       | Event profiler (development)                    | tracy              | 0.11.1#2        | yes :green_circle:  |
| [zlib-ng](https://github.com/zlib-ng/zlib-ng)            | ZMBV video capture                              | zlib-ng            | 2.2.4#1         | yes :green_circle:  |


### Dynamically loaded dependencies

**FluidSynth** and **Slirp** are built separately as dynamic libraries (via
vcpkg) and are loaded on-demand at runtime if available. Please refer to the
[dosbox-staging-ext](https://github.com/dosbox-staging/dosbox-staging-ext)
project for further details. To check the versions of these dependencies
included in our stable and dev build packages, check out the comments of the
[releases packages](https://github.com/dosbox-staging/dosbox-staging-ext/releases) of
the [dosbox-staging-ext](https://github.com/dosbox-staging/dosbox-staging-ext)
project.

For information on the optional **Nuked SC-55 CLAP** audio plugin, check out
the [plugin's GitHub repository](https://github.com/johnnovak/Nuked-SC55-CLAP/) (note Nuked SC-55
CLAP is _not_ a DOSBox Staging project).


## Get the sources

Clone the repository (one-time step):

``` shell
git clone https://github.com/dosbox-staging/dosbox-staging.git
```

## Build instructions

Please refer to the platform specific build instructions:

- [Windows](docs/build-windows.md)
- [macOS](docs/build-macos.md)
- [Linux](docs/build-linux.md)

> **Note**
>
> CMake support is currently an experimental internal-only, work-in-progress
> feature; it's not ready for public consumption yet. Please ignore the
> `CMakeLists.txt` files in the source tree.


### Linux, macOS

Install build dependencies appropriate for your OS:

``` shell
# Fedora
sudo dnf install ccache gcc-c++ meson alsa-lib-devel libatomic libpng-devel \
                 SDL2-devel SDL2_net-devel opusfile-devel \
                 fluidsynth-devel iir1-devel mt32emu-devel libslirp-devel \
                 speexdsp-devel libXi-devel zlib-ng-devel
```

``` shell
# Debian, Ubuntu
sudo apt install ccache build-essential libasound2-dev libatomic1 libpng-dev \
                 libsdl2-dev libsdl2-net-dev libopusfile-dev \
                 libfluidsynth-dev libslirp-dev libspeexdsp-dev libxi-dev

# Install Meson on Debian-11 "Bullseye" or Ubuntu-21.04 and newer
sudo apt install meson
```

``` shell
# Arch, Manjaro
sudo pacman -S ccache gcc meson alsa-lib libpng sdl2 sdl2_net \
               opusfile fluidsynth libslirp speexdsp libxi pkgconf
```

``` shell
# openSUSE
sudo zypper install ccache gcc gcc-c++ meson alsa-devel libatomic1 libpng-devel \
                    libSDL2-devel libSDL2_net-devel \
                    opusfile-devel fluidsynth-devel libmt32emu-devel libslirp-devel \
                    speexdsp libXi-devel
```

``` shell
# macOS
xcode-select --install
brew install cmake ccache meson libpng sdl2 sdl2_net opusfile \
     fluid-synth libslirp pkg-config python3 speexdsp
```

### Build and stay up-to-date with the latest sources

1. Check out the main branch:

    ``` shell
    # commit or stash any personal code changes
    git checkout main -f
    ```

2. Pull the latest updates. This is necessary every time you want a new build:

    ``` shell
    git pull
    ```

3. Set up the build. This is a one-time step either after cloning the repo or
    cleaning your working directories:

    ``` shell
    meson setup build
    ```

    The above enables all of DOSBox Staging's functional features. If you're
    interested in seeing all of Meson's setup options, run `meson configure`.

4. Compile the sources. This is necessary every time you want a new build:

    ``` shell
    meson compile -C build
    ```

    Your binary is: `build/dosbox`

    The binary depends on local resources relative to it, so we suggest
    symlinking to the binary from your `PATH`, such as into `~/.local/bin/`.


### Windows – Visual Studio (2022 or newer)

First, you need to setup [vcpkg] to install build dependencies. Once vcpkg
is bootstrapped, open PowerShell and run:

``` powershell
PS:\> .\vcpkg integrate install
```

This step will ensure that MSVC can use vcpkg to build, find and links all
dependencies.

Start Visual Studio and open the file `vs\dosbox.sln`. Make sure you have
`x64` selected as the solution platform.  Use **Ctrl+Shift+B** to build all
projects.

Note, the first time you build a configuration, dependencies will be built
automatically and stored in the `vcpkg_installed` directory. This can take
a significant length of time.

[vcpkg]: https://github.com/microsoft/vcpkg


### Windows (MSYS2), macOS (MacPorts), Haiku, NixOS, others

Instructions for other build systems and operating systems are documented
in [docs/BUILD.md].

Links to OS-specific instructions: [MSYS2], [MacPorts], [Haiku], [NixOS].

[BUILD.md]: docs/BUILD.md
[MSYS2]:    docs/build-windows.md
[MacPorts]: docs/build-macos.md
[Haiku]:    docs/build-haiku.md
[NixOS]:    docs/build-nix.md


## Imported branches, community patches, old forks

Upstream commits are imported to this repo in a timely manner,
see branch [`svn/trunk`].

- [`svn/*`] - branches from SVN
- [`forks/*`] - code for various abandoned DOSBox forks
- [`vogons/*`] - community patches posted on the Vogons forum

Git tags matching pattern `svn/*` are pointing to the commits referenced by SVN
"tag" paths at the time of creation.

Additionally, we attach some optional metadata to the commits in the form of
[Git notes][git-notes]. To fetch them, run:

``` shell
git fetch origin "refs/notes/*:refs/notes/*"
```

[`svn/*`]:     https://github.com/dosbox-staging/dosbox-staging/branches/all?utf8=%E2%9C%93&query=svn%2F
[`svn/trunk`]: https://github.com/dosbox-staging/dosbox-staging/tree/svn/trunk
[`vogons/*`]:  https://github.com/dosbox-staging/dosbox-staging/branches/all?utf8=%E2%9C%93&query=vogons%2F
[`forks/*`]:   https://github.com/dosbox-staging/dosbox-staging/branches/all?utf8=%E2%9C%93&query=forks%2F
[git-notes]:   https://git-scm.com/docs/git-notes

[gpl-badge]:     https://img.shields.io/badge/license-GPL--2.0--or--later-blue
[discord-badge]: https://img.shields.io/discord/514567252864008206?color=%237289da&logo=discord&logoColor=white&label=discord
[discord]:       https://discord.gg/WwAg3Xf

[build-lin1-badge]: https://img.shields.io/github/actions/workflow/status/dosbox-staging/dosbox-staging/linux.yml?label=Linux%20%28x86_64%29
[build-lin1-ci]:    https://github.com/dosbox-staging/dosbox-staging/actions/workflows/linux.yml?query=branch%3Amain

[build-lin2-badge]: https://img.shields.io/github/actions/workflow/status/dosbox-staging/dosbox-staging/platforms.yml?label=Linux%20%28other%29
[build-lin2-ci]:    https://github.com/dosbox-staging/dosbox-staging/actions/workflows/platforms.yml?query=branch%3Amain

[build-win-msys2-badge]: https://img.shields.io/github/actions/workflow/status/dosbox-staging/dosbox-staging/windows-msys2.yml?label=Windows%20%28MSYS2%29
[build-win-msys2-ci]:    https://github.com/dosbox-staging/dosbox-staging/actions/workflows/windows-msys2.yml?query=branch%3Amain

[build-win-msvc-badge]: https://img.shields.io/github/actions/workflow/status/dosbox-staging/dosbox-staging/windows-msvc.yml?label=Windows%20%28Visual%20Studio%29
[build-win-msvc-ci]:    https://github.com/dosbox-staging/dosbox-staging/actions/workflows/windows-msvc.yml?query=branch%3Amain

[build-mac-badge]: https://img.shields.io/github/actions/workflow/status/dosbox-staging/dosbox-staging/macos.yml?label=macOS%20%28x86_64%2C%20arm64%29
[build-mac-ci]:    https://github.com/dosbox-staging/dosbox-staging/actions/workflows/macos.yml?query=branch%3Amain


## Website & documentation

Please refer to the [documentation guide](DOCUMENTATION.md) before making
changes to the website or the documentation.
