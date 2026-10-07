# Implementation notes

## Source layout

```text
src/
├── freecam.cppm             # lifecycle, Mod Menu, hook/runtime orchestration
└── freecam/
    ├── debug.cppm           # separate diagnostic module + selectable log level
    ├── ecs.cppm             # exact EntityId/EntityContext/EnTT mirror
    └── target.cppm          # 1.26.52.3 Build ID + ClientInstance ABI gate
```

The project does not copy BedrockTools sources. Its Xmake dependency boundary is adopted from BedrockTools: Xmake, Android preloader package, EnTT and fmt.

## Diagnostic levels

The `levi_freecam.Debug` module is disabled by default. Its `Debug Level` slider is:

- `0`: Off
- `1`: Errors
- `2`: Info/state transitions
- `3`: Verbose hook/player/ECS processing
- `4`: Trace, including every update hook pass

This is intentionally separate from the Freecam module so normal use does not produce per-frame logs.

## Release packaging

CI continues to upload the `.levipack` and `.so` as workflow artifacts. Tagged builds additionally publish both files as GitHub Release assets, so the `.levipack` has a stable direct download URL.
