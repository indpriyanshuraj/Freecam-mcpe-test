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

The output is `build/out/liblevi_freecam.levipack`. Build logs are written under `build/logs/`.

## GitHub Actions

The repository includes `.github/workflows/build.yml`. Every push to `main`/`master` and every pull request builds the Android `arm64-v8a` release using the same Xmake commands used locally. The workflow follows the BedrockTools build pattern: setup Xmake, setup Android NDK r28c, configure with `xmake f`, then build/package with `xmake`.

The workflow verifies the produced library and Levipack before uploading both artifacts.

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

The implementation uses `src/freecam.cppm` as a C++23 module interface and `src/main.cpp` imports it. Xmake's C++ module dependency analysis is enabled explicitly.

## Important runtime behavior

The Freecam ModMenu module is enabled by default and creates a HUD toggle button. Disabling that module removes the button and requests native debug-camera shutdown.

The mod automatically disables Freecam when the local player or its ECS registry changes, preventing state from leaking across world/dimension/player replacement.

If vanilla/native debug camera was already active before this mod enabled, the mod does not claim ownership and therefore does not remove the pre-existing global component when disabled.

## Remaining runtime validation

The binary RE is complete enough to eliminate BedrockTools and use the native ECS trigger directly. The remaining test that requires a real Android client is behavioral: verify that the native `DebugCameraIsActiveComponent` activation path causes the expected free-camera input mode on the exact installed 1.26.52.3 build, including mobile touch controls, and verify restoration while walking, riding, swimming, falling, and creative flying.

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
