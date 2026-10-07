# Build status

Target: Minecraft Bedrock Android `1.26.52.3`

Target ELF Build ID:

`56de9eed077631e03a31f4f58eb2f0e00071d335`

## Verified target gate

- `ClientInstance::update(bool)` exact signature: `0x098036A4`
- `ClientInstance::getLocalPlayer() const` exact signature: `0x09808050`
- `ClientInstance` RTTI name: `14ClientInstance`
- ClientInstance vtable relocations: verified
- exact Build ID: verified

## Current runtime strategy

1. Native mod loads without requiring `libminecraftpe.so` to already exist.
2. A runtime watcher waits for the exact target library.
3. Build ID and ClientInstance RTTI/vtable are checked before the hook is installed.
4. `IClientInstance::update(bool)` is hooked.
5. The live ClientInstance vtable is rechecked before processing.
6. `getLocalPlayer()` resolves the current local player.
7. The player's `EntityContext` is mirrored at `Actor + 0x08`.
8. The EnTT registry is obtained from `EntityContext::mEnTTRegistry`.
9. The mod requests `DebugCameraIsActiveComponent` in the registry context without changing GameType or teleporting the player.

## Important limitation

The native component path is RE-backed but still requires real-device behavioral confirmation that the exact `1.26.52.3` debug-camera systems consume only this global state for the desired freecam behavior. The new debug module is intended to distinguish a UI/request/hook/ECS failure from a camera-state implementation failure.
