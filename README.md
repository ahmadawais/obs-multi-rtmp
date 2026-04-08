# obs-multi-rtmp-aa

Stream to multiple RTMP / SRT / WHIP endpoints simultaneously from a single
OBS Studio scene. This is a **ground-up C++20 rewrite** of
[sorayuki/obs-multi-rtmp](https://github.com/sorayuki/obs-multi-rtmp),
installable side-by-side with the original.

Built on the OBS plugin template. Same persistence format as upstream — your
old configs load without migration.

## Why this fork exists

The upstream plugin works, but the codebase had grown organically and was
hard to reason about:

- `shared_ptr` for every config struct
- A single 1,163-line edit dialog re-implementing OBS's property UI by hand
- Manual `obs_*_release` calls sprinkled throughout business logic
- File-scope singletons for config and UI-thread posting
- Mixed concerns: one class was both a `QWidget` and an OBS signal handler

This fork rewrites every line of `src/` from scratch with a cleaner
architecture, then proves it by building cleanly on macOS, Windows, and
Linux with the **same** CI pipeline the original uses.

## What changed

| Area | Upstream | This rewrite |
|---|---:|---:|
| Total LOC in `src/` | 3,546 | **2,083** (−41%) |
| Largest file | 1,163 (edit dialog) | 423 | (−64%)|
| Config model | `shared_ptr` + `std::list` | value types + `std::vector` |
| OBS handle ownership | manual release calls | RAII smart pointers |
| Threading | ad-hoc global singleton | injected UI poster |
| Edit dialog | hand-rolled tabs + manual forms | delegates to reusable `PropertiesView` |
| C++ standard | C++17-ish | C++20 throughout |
| Error codes | magic `-1/-2/-3` | real `OBS_OUTPUT_*` constants |

See [`oldsrc/`](./oldsrc) for the preserved upstream source, and
`git log rewrite/from-scratch` for the full rewrite history.

### Architecture

```
src/
├── Plugin.cpp           module entry, dock registration
├── Logging.h            blog() macros
├── ObsPtr.h             RAII smart pointers for obs_*_t handles
├── Config.{h,cpp}       value-type model + JSON persistence
├── Protocols.{h,cpp}    RTMP / SRT / WHIP registry
├── OutputSession.{h,cpp}  one streaming target: state machine + stats
└── ui/
    ├── Dock.{h,cpp}           main dock widget
    ├── TargetRow.{h,cpp}      one row per target
    ├── PropertiesView.{h,cpp} obs_properties_t → QFormLayout
    └── EditDialog.{h,cpp}     add/edit modal
```

Design principles applied throughout:

- **RAII over every OBS handle.** No manual `obs_*_release` outside
  `ObsPtr.h`.
- **Value types for config.** No `shared_ptr` soup.
- **Strict separation of concerns.** `OutputSession` has zero Qt
  dependencies; `TargetRow` has zero direct OBS API calls.
- **UI thread marshalling via `std::function`.** OBS signals post to Qt
  via an injected `UiPoster`, not a global singleton.
- **No globals.** `Dock` owns the `MultiOutputConfig` directly.
- **C++20 idioms.** `std::erase_if`, `contains`, designated initializers,
  `std::span`, `string_view` at boundaries.

## Side-by-side with upstream

This plugin is namespaced with an `-aa` suffix everywhere it would
otherwise collide with the upstream plugin:

| Thing | Upstream | This fork |
|---|---|---|
| Bundle name | `obs-multi-rtmp.plugin` | `obs-multi-rtmp-aa.plugin` |
| macOS bundle id | `net.sorayuki.obs-multi-rtmp` | `com.ahmadawais.obs-multi-rtmp-aa` |
| OBS dock id | `obs-multi-rtmp-dock` | `obs-multi-rtmp-aa-dock` |
| Dock title | `Multiple output` | `Multiple output AA` |
| Profile config file | `obs-multi-rtmp.json` | `obs-multi-rtmp-aa.json` |
| Log tag | `[obs-multi-rtmp]` | `[obs-multi-rtmp-aa]` |
| Windows installer | `obs-multi-rtmp-setup.exe` | `obs-multi-rtmp-aa-setup.exe` |

Install both simultaneously and they will not touch each other's state.

## Install

### macOS

Download the latest macOS build from the
[Actions tab](https://github.com/ahmadawais/obs-multi-rtmp/actions) →
pick a successful run → scroll to **Artifacts** → grab
`obs-multi-rtmp-aa-<version>-macos-universal-<sha>`.

Then run the provided installer script:

```bash
./scripts/install-macos.sh
```

It will:
1. Auto-find the newest `obs-multi-rtmp-aa*.zip` in `~/Downloads`
2. Unwrap the nested `.zip` → `.tar.xz` → `.plugin` bundle
3. Print the architectures to confirm it's a universal binary
4. Quit OBS if it's running (with confirmation)
5. Copy the bundle into `~/Library/Application Support/obs-studio/plugins/`
6. Strip `com.apple.quarantine` so Gatekeeper doesn't silently reject it

You can also pass an explicit path:

```bash
./scripts/install-macos.sh path/to/obs-multi-rtmp-aa.zip
./scripts/install-macos.sh path/to/obs-multi-rtmp-aa.plugin
```

After install, open OBS → **Docks** → enable **Multiple output AA**.

### Windows

Download `obs-multi-rtmp-aa-<version>-windows-x64-<sha>` from the Actions
artifacts, unzip, and run `obs-multi-rtmp-aa-<version>-Installer.exe`.

### Linux (Ubuntu)

Download the Ubuntu artifact and install the `.deb`:

```bash
sudo dpkg -i obs-multi-rtmp-aa_*.deb
```

## Build from source

```bash
# macOS
cmake --preset macos
cmake --build build_macos --config RelWithDebInfo

# Windows
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo

# Ubuntu
cmake --preset ubuntu-x86_64
cmake --build build_x86_64
```

Deps (OBS sources, libobs prebuilt, Qt6 prebuilt) are fetched automatically
by the OBS plugin template during configure.

Requires:

- CMake 3.28+
- On macOS: Xcode 15+ (the preset uses the Xcode generator)
- On Windows: Visual Studio 2022
- On Linux: GCC 12+ or Clang 15+

## Status

- ✅ Builds clean on macOS, Windows, Ubuntu (GitHub Actions)
- ✅ Feature parity with upstream for RTMP / SRT / WHIP outputs
- ✅ Loads upstream configs unchanged
- ⚠️ Not code-signed — macOS builds require `xattr -dr com.apple.quarantine`
  (the install script does this)
- 🚧 Per-target scene override and per-track audio mapping are supported by
  the config layer and by the streaming pipeline, but not exposed in the
  edit dialog UI yet (the v1 pass kept the dialog minimal). Edit
  `obs-multi-rtmp-aa.json` in your profile directory to use them until the
  UI catches up.

## Credits

- **Original plugin:** [SoraYuki](https://github.com/sorayuki) —
  [sorayuki/obs-multi-rtmp](https://github.com/sorayuki/obs-multi-rtmp)
- **Rewrite:** [Ahmad Awais](https://github.com/ahmadawais)
- **OBS Studio plugin template:** https://github.com/obsproject/obs-plugintemplate

## License

GPL-2.0-or-later, matching upstream. See [LICENSE](./LICENSE).
