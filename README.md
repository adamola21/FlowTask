# FlowTask

A tiny macro recorder for Windows. It records your mouse movement, clicks, scrolling and
keystrokes, then replays them exactly — with smooth, interpolated cursor motion instead of
the usual teleporting jumps.

No installer, no account, no background service. One 200 KB `.exe`.

---

## Features

- **Records everything** — mouse movement, left/right clicks, scroll wheel, keyboard
- **Smooth playback** — the cursor follows a Catmull-Rom spline through the recorded path
  at 1 ms timer resolution, so movement looks natural rather than jumpy
- **Playback speed** — ½x, 1x, 2x, 100x or a custom multiplier
- **Loops** — repeat a macro a set number of times, or continuously
- **Global hotkeys** — work even when the window is in the background
- **Save / load** macros as `.flow` files
- **Always on top**, compact mode, dark UI

## Hotkeys

| Key | Action |
|-----|--------|
| `F9`  | Start / stop recording |
| `F10` | Start / stop playback |
| `F8`  | **Panic stop** — aborts everything immediately |

All three are configurable under **Prefs**. `F8` is the one that gets you out of
continuous playback when the cursor is moving on its own.

## Download

Grab the latest `FlowTask.exe` from the [Releases](../../releases) page.

Windows will show a blue *"Windows protected your PC"* box the first time you run it —
click **More info → Run anyway**. This appears for any application that isn't
code-signed; a signing certificate costs several hundred euros a year, which is why
small free tools generally don't have one.

## Why does my antivirus complain?

To capture keystrokes while other windows have focus, FlowTask installs a
[low-level keyboard hook](https://learn.microsoft.com/en-us/windows/win32/winmsg/about-hooks) —
the standard Windows mechanism for this. Legitimate macro tools like TinyTask and
AutoHotkey use exactly the same API, but so do keyloggers, so heuristic scanners
sometimes flag the whole category.

That's precisely why this repository exists: the entire program is a single readable
C file, [`src/main.c`](src/main.c). You can verify for yourself that it

- makes **no network calls** whatsoever (no sockets, no HTTP, no DNS — nothing is imported)
- writes **nothing** outside the file you explicitly pick in the Save dialog
- creates **no** registry entries, scheduled tasks or autostart hooks
- keeps recorded events in memory only, and discards them when you close it

Releases are built by GitHub Actions from this exact source
([workflow](.github/workflows/build.yml)), so the published binary is not something
handed over by a stranger — it is compiled in public from the code you can read.

## Building it yourself

The build is a single compiler invocation. On any Linux machine with MinGW:

```sh
sudo apt-get install mingw-w64
./build.sh
```

Or on Windows with MinGW-w64 installed:

```sh
windres -I res res/app.rc -O coff -o app.res
gcc -O2 -mwindows -o FlowTask.exe src/main.c app.res -static \
    -lcomctl32 -lcomdlg32 -lmsimg32 -lwinmm
```

No dependencies beyond the Win32 API.

## License

MIT — see [LICENSE](LICENSE). Do what you like with it.
