module;

#include <cstddef>
#include <cstdint>
#include <entt/entt.hpp>

export module levi.freecam.ecs;

export enum class EntityId : std::uint32_t {};

export struct EntityIdTraits {
    using value_type = EntityId;
    using entity_type = std::uint32_t;
    using version_type = std::uint16_t;
    static constexpr entity_type entity_mask = 0x3FFFF;
    static constexpr entity_type version_mask = 0x3FFF;
};

namespace entt::internal {
template <> struct entt_traits<::EntityId> : ::EntityIdTraits {};
}

export struct EntityContextMirror {
    std::uintptr_t mRegistry{};
    std::uintptr_t mEnTTRegistry{};
    EntityId mEntity{};
};

export struct DebugCameraIsActiveComponent {};

static_assert(sizeof(EntityId) == sizeof(std::uint32_t));
static_assert(sizeof(EntityContextMirror) == 0x18);
static_assert(offsetof(EntityContextMirror, mEnTTRegistry) == 0x08);
static_assert(offsetof(EntityContextMirror, mEntity) == 0x10);
static_assert(entt::type_hash<DebugCameraIsActiveComponent>::value() == 0x25D8BF40u);

export namespace levi_freecam::ecs {
using Registry = entt::basic_registry<EntityId>;

inline EntityContextMirror* contextFromPlayer(void* player, std::uintptr_t offset) noexcept {
    if (!player) return nullptr;
    return reinterpret_cast<EntityContextMirror*>(reinterpret_cast<std::uintptr_t>(player) + offset);
}

inline Registry* registryFromPlayer(void* player, std::uintptr_t offset) noexcept {
    const auto* context = contextFromPlayer(player, offset);
    return context && context->mEnTTRegistry
        ? reinterpret_cast<Registry*>(context->mEnTTRegistry)
        : nullptr;
}
} // namespace levi_freecam::ecs
