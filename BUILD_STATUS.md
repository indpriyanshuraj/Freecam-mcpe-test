# Build status

## Build architecture

This project intentionally follows the proven BedrockTools build architecture rather than copying its project files:

- Xmake is the project build system.
- GitHub Actions installs Xmake with the official Xmake setup action.
- `nttld/setup-ndk` supplies the Android NDK and its reported `ndk-path` is passed directly to Xmake.
- The preloader package is built from its current `main` branch, matching the dependency choice used by BedrockTools.
- EnTT is pinned to v4.0.0 because the Freecam implementation depends on its current `EntityId`/registry integration.
- The mod keeps its own C++23 module source, RE validation, lifecycle handling, and Levipack packaging.

## CI diagnosis

The previous CI failure was not an NDK-path failure. The compiler was invoked successfully from NDK r30 and the failure occurred while building the **old pinned preloader 0.2.3** dependency:

`fmt::format.h:747:28: error: use of undeclared identifier 'malloc'`

BedrockTools currently uses the preloader `main` package recipe and NDK r28c. The project now adopts those dependency/toolchain choices instead of adding custom compiler or NDK path handling.
