# Implementation notes

The native implementation is kept in `src/freecam.cppm` as a C++23 module unit. Mod registration is kept in that same unit so no translation unit imports the module and then includes Preloader headers. Xmake's C++ module dependency analysis is enabled explicitly.

The Android build intentionally follows the BedrockTools workflow rather than introducing a separate CMake build orchestration layer. The preloader dependency is built through Xmake's standard CMake package helper, and EnTT `v4.0.0` is consumed as a package.

For Termux, set `ANDROID_NDK_HOME` and run:

```sh
./scripts/build.sh release
```

The script is only a thin wrapper around the same Xmake configure/build commands used by CI.


## ModMenu state

The module exposes an `Enabled` toggle configuration with default value `true`. The HUD button is a Toggle button and receives `StateChanged`; both paths update the same atomic native request. Runtime logs report only state transitions and first ECS binding, not every frame.

## Metadata

The packaged manifest identifies the author as `Levi` and version `1.0.0`, targeting Minecraft `1.26.52.3`.
