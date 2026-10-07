# Levi Freecam — Minecraft Bedrock 1.26.52.3

Standalone Android native Freecam for LeviLaunchroid/LeviLamina. **BedrockTools is not a dependency.**

## Design

The mod does not change GameType, teleport the player, rewrite movement packets, or copy the free-camera position back to the player.

The HUD toggle only changes an atomic request flag. On the Minecraft client update thread, the mod resolves the local player's `EntityContext`, obtains the same EnTT registry used by the client systems, and adds/removes the native global `DebugCameraIsActiveComponent`.

Bedrock's own debug-camera, camera-input, free-camera movement, and player movement-filter systems then run normally. This is the intended native path identified from the 1.26.52.3 ARM64 client.

## Exact target gate

- Minecraft: `1.26.52.3`
- ELF Build ID: `56de9eed077631e03a31f4f58eb2f0e00071d335`
- ABI: `arm64-v8a`
- `ClientInstance` vtable address point RVA: `0x12BC3300`
- RTTI RVA: `0x12BC42B8`
- RTTI name RVA: `0x02BE2BEB` → `14ClientInstance`
- `IClientInstance::update(bool)` vtable slot: `25` → RVA `0x098036A4`
- `IClientInstance::getLocalPlayer() const` vtable slot: `32` → RVA `0x09808050`
- `Actor::mEntityContext`: `0x08`
- `DebugCameraIsActiveComponent` EnTT type hash: `0x25D8BF40`

Every target is validated at runtime. Any ABI mismatch fails closed.

## Build in Termux

Install the build tools once:

```sh
pkg install xmake ninja git python3
```

Set the Android NDK if it is not autodetected:

```sh
export ANDROID_NDK_ROOT="$HOME/Android/Sdk/ndk/28.2.13676358"
```

Build and package:

```sh
bash scripts/build.sh
```

Debug build:

```sh
bash scripts/build.sh debug
```

Clean:

```sh
bash scripts/build.sh clean
```

The release output directory is `build/android/arm64-v8a/release/` and contains:

- `liblevi_freecam.so`
- `levi_freecam.levipack`

The build script prints the major build/package phases without forcing verbose compiler-command output.

## GitHub Actions

The repository includes `.github/workflows/build.yml`. Every push to `main`/`master` and every pull request builds the Android `arm64-v8a` release using the same Xmake commands used locally. The workflow follows the BedrockTools build pattern: setup Xmake, setup Android NDK r28c, configure with `xmake f`, then build/package with `xmake`.

The workflow verifies the produced library and Levipack before uploading them as two separate GitHub Actions artifacts: one `.so` artifact and one `.levipack` artifact.

## Dependencies

Runtime:

- Levi preloader Android SDK from its current `main` branch

Build:

- EnTT `v4.0.0`
- Xmake
- Android NDK
- Python 3 (package creation)

No BedrockTools library, headers, API, or runtime bridge are used.

## C++23 modules

The implementation uses `src/freecam.cppm` as a C++23 module interface. Mod registration is emitted from that same module unit, avoiding a second translation unit that imports the module and then re-includes Preloader headers. Xmake's C++ module dependency analysis is enabled explicitly.

## Important runtime behavior

The Freecam ModMenu module is enabled by default. It exposes an `Enabled` toggle setting whose default is `true`; this controls the native free-camera request. A separate HUD toggle button controls the same runtime request. The HUD button is an SVG-only sharp `F` icon with a transparent interior/background; the text label is hidden. Disabling the module removes the button and requests native debug-camera shutdown.

The mod automatically disables Freecam when the local player or its ECS registry changes, preventing state from leaking across world/dimension/player replacement.

If vanilla/native debug camera was already active before this mod enabled, the mod does not claim ownership and therefore does not remove the pre-existing global component when disabled.

## Remaining runtime validation

The binary RE is complete enough to eliminate BedrockTools and use the native ECS trigger directly. The exact ClientInstance signatures are also checked byte-for-byte against the BedrockTools signatures on the supplied ELF. The remaining test that requires a real Android client is behavioral: verify that the native `DebugCameraIsActiveComponent` activation path causes the expected free-camera input mode on the exact installed 1.26.52.3 build, including mobile touch controls, and verify restoration while walking, riding, swimming, falling, and creative flying.

## Optional target verification

When you have a local copy of the exact Minecraft library:

```sh
python3 scripts/verify_layout.py /path/to/libminecraftpe.so
```

It checks the supplied Build ID before you proceed with runtime testing.

## Current RE boundary

Static RE is complete for the independent implementation. Exact `CameraControlSchemeSet/Clear` callable entrypoints were not required because the native `DebugCameraIsActiveComponent` already feeds the proven debug-camera and movement-filter systems. Device testing remains necessary to confirm touch-input behavior and all restore scenarios on the exact client build.

## Dependency versions

This project uses the current Levi Android preloader `main` branch and EnTT `v4.0.0` to match the current LeviLamina ECS definitions and the proven BedrockTools Android dependency combination.

## Build dependencies

Xmake retrieves the Levi Android preloader through its normal CMake package helper and EnTT `v4.0.0`; the dependency/toolchain choices follow the BedrockTools pattern without copying its source.

## C++ runtime

The mod uses `c++_shared`, matching the Android preloader runtime boundary.

## Debug module

`Freecam Debug` is a separate Mod Menu module and is **off by default**. Enable it only while diagnosing the native runtime.

| Level | Meaning |
|---:|---|
| 0 | Off |
| 1 | Errors only |
| 2 | Lifecycle and state transitions |
| 3 | Hook, player, registry and ECS processing |
| 4 | Trace: every `ClientInstance::update` processing step |

The diagnostic path reports, in order: Minecraft library discovery, exact Build ID validation, ClientInstance RTTI/vtable validation, hook installation, ClientInstance validation on the live object, local-player resolution, EntityContext/ECS registry binding, request state, `DebugCameraIsActiveComponent` lookup, insertion/removal, and lifecycle/world/player changes.

Use level 4 only for short reproduction windows because it logs every hooked update.

## GitHub downloads

Tagged builds publish the complete `.levipack` as a GitHub Release asset. The workflow prints the direct installer URL in the job summary:

`https://github.com/<owner>/<repo>/releases/download/<tag>/levi_freecam.levipack`

Replace `<tag>` with the published tag, for example `v1.0.0`.


## Build modes

The project supports both Xmake build modes:

```text
scripts/build.sh debug
scripts/build.sh release
scripts/build.sh clean
```

GitHub Actions uses `debug` for normal commits, pull requests, and manual runs. A pushed `v*` tag uses `release` and publishes `levi_freecam.levipack` as a GitHub Release asset with a direct download URL.

## Debug module

`Freecam Debug` is a separate Mod Menu module and is disabled by default. Its `Debug Level` is:

- `0` — Off
- `1` — Errors
- `2` — Lifecycle/state changes
- `3` — Hook/player/ECS processing
- `4` — Trace, including every client update

Use level 4 when diagnosing a device where the Freecam button is visible but native camera state does not change.
