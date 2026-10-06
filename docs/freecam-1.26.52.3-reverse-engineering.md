# Freecam 1.26.52.3 — Independent Native RE Record

## Final implementation route

The independent implementation no longer needs BedrockTools. The validated route is:

```text
ModMenu toggle
    ↓ atomic request flag
ClientInstance::update(bool) hook
    ↓ validated ClientInstance object
IClientInstance::getLocalPlayer() const
    ↓ Actor::mEntityContext + 0x08
EntityContext::mEnTTRegistry
    ↓ entt::basic_registry<EntityId>::ctx()
DebugCameraIsActiveComponent
    ↓
Bedrock DebugCameraSystem + CameraFlyMoveSystem
    ↓
PlayerMoveSystemsImpl debug-camera filter
```

This leaves the real actor position/state alone. The native camera systems own the camera movement.

## Exact Android 1.26.52.3 facts

### ELF

- ELF64 / AArch64 / ET_DYN
- Build ID: `56de9eed077631e03a31f4f58eb2f0e00071d335`
- `.text`: RVA `0x64C64E0`, size `0xC44AFCC`
- `.rodata`: RVA `0x2311800`
- `.data.rel.ro`: file/virtual mapping was inspected while resolving the `ClientInstance` vtable.

### ClientInstance vtable

The vtable pointers are runtime-relocated in the Android ELF, so the source binary does not contain final absolute pointer values at those slots. The RE used ELF relocation metadata to establish the table/typeinfo/function relationships, and the mod re-validates the resulting live pointers at runtime before hooking.


Validated address point:

`0x12BC3300`

The word at `address_point - 0x08` is the RTTI pointer:

`0x12BC42B8`

The RTTI object's type-name pointer resolves to:

`0x02BE2BEB` → `14ClientInstance`

The vtable also validates the known functions:

| vtable slot | API | RVA |
|---:|---|---:|
| 25 | `IClientInstance::update(bool)` | `0x098036A4` |
| 32 | `IClientInstance::getLocalPlayer() const` | `0x09808050` |

The slot convention was cross-checked against the existing BedrockTools client-instance vtable constants, where `getLocalPlayer` is slot 32.

### EntityContext

LeviLamina declares `EntityContext` as:

```cpp
EntityRegistry& mRegistry;
entt::basic_registry<EntityId>& mEnTTRegistry;
EntityId const mEntity;
```

On AArch64, the two reference members occupy 8 bytes each, so the second reference is at `+0x08`. The mod therefore uses a pointer-only mirror and never constructs or calls the full class.

### EnTT

Current LeviLamina pins EnTT `v4.0.0`.

`registry.ctx()` exposes a process/world ECS global context with:

```cpp
contains<T>()
emplace<T>()
erase<T>()
```

The exact global component is:

```cpp
struct DebugCameraIsActiveComponent {};
```

Exact type hash verified with the Clang/EnTT type-name mechanism:

`0x25D8BF40`

The mod contains a compile-time assertion for this value.

## Native camera architecture confirmed from the client

### DebugCameraSystem

The strict-system signature contains:

- `MinecraftCamera::RenderCameraComponent`
- `MinecraftCamera::DebugCameraComponent`
- optional `MinecraftCamera::CameraComponent`
- `MinecraftCamera::GameCameraComponent`
- optional global `DebugCameraIsActiveComponent`

### Player movement debug-camera filter

The strict-system signature contains:

- `LocalPlayerComponent`
- `MoveRequestComponent`
- optional global `DebugCameraIsActiveComponent`

This is the decisive evidence that the same global ECS component disables the ordinary player movement request path while the debug camera is active.

### CameraFlyMoveSystem

The strict-system signature contains:

- `MinecraftCamera::CameraFlyMoveComponent`
- `MinecraftCamera::CameraComponent`
- optional global `CameraAPIComponent`
- optional `MinecraftCamera::CameraTimeComponent`

### CameraInputTransferSystem

`_tickMoveInputUpdate` contains:

- `MinecraftCamera::ActiveCameraComponent`
- `MinecraftCamera::CameraComponent`
- `MinecraftCamera::CameraAttachComponent`
- `MinecraftCamera::DefaultInputCameraComponent`
- `ActorMovementTickNeededComponent`
- `LocalPlayerComponent`
- `MoveInputComponent`

`_tickPlayerActionUpdate` contains:

- `ActorMovementTickNeededComponent`
- `LocalPlayerComponent`
- `PlayerActionComponent`
- `ActorRotationComponent`

## Camera-control strings found

- `CameraControlSchemeSet` RVA `0x2511CD9` — verified as a string anchor, not a callable entry.
- `CameraControlSchemeClear` RVA `0x239EC56` — verified as a string anchor, not a callable entry.
- `WASD_FREE_CAMERA_CONTROLLED` RVA `0x35FBD1E`.
- `minecraft:free_camera_controlled` RVA `0x23A0695`.

Exact callable entries for the camera-control scheme helpers were not conclusively needed for the final implementation because the native debug-camera activation component already feeds the camera and movement systems that were proven above.

## What is deliberately not used

- no GameType spectator switch
- no player teleport
- no player-position copying
- no packet filtering as the primary design
- no chunk-loading exploit
- no direct `CameraComponent` offset writes
- no guessed camera vtable hook
- no BedrockTools runtime dependency

## Ownership rules

The mod tracks whether it inserted `DebugCameraIsActiveComponent` into a registry.

If the component already exists, the mod does not claim it and does not erase it on disable/unload.

If the player or ECS registry changes, the mod clears its ownership bookkeeping without dereferencing the old registry.

## Remaining real-device validation

No static RE gap remains that requires BedrockTools. A physical Android run is still required to validate:

1. native free-camera movement from mobile touch controls;
2. camera movement through already loaded client chunks;
3. absence of real-player motion while freecam is active;
4. safe return while walking/riding/swimming/falling/creative-flying;
5. behavior across dimension/world transitions and pause/menu transitions.

Those are behavioral validations, not missing source signatures.
