# Implementation notes

The native implementation is kept in `src/freecam.cppm` with `src/main.cpp` importing the module. Xmake's C++ module dependency analysis is enabled explicitly.

The Android build intentionally follows the BedrockTools workflow rather than introducing a separate CMake build orchestration layer. The preloader dependency is built through Xmake's standard CMake package helper, and EnTT `v4.0.0` is consumed as a package.

For Termux, set `ANDROID_NDK_HOME` and run:

```sh
./scripts/build.sh release
```

The script is only a thin wrapper around the same Xmake configure/build commands used by CI.
