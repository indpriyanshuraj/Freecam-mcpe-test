module;

#include <array>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <link.h>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <entt/entt.hpp>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>

export module levi.freecam;

// -----------------------------------------------------------------------------
// Exact 1.26.52.3 Bedrock ECS mirror.
//
// These declarations intentionally mirror only ABI facts required by this mod.
// They are not replacements for the full LeviLamina SDK.
// -----------------------------------------------------------------------------

enum class EntityId : std::uint32_t {};

struct EntityIdTraits {
    using value_type = EntityId;
    using entity_type = std::uint32_t;
    using version_type = std::uint16_t;
    static constexpr entity_type entity_mask = 0x3FFFF;
    static constexpr entity_type version_mask = 0x3FFF;
};

namespace entt::internal {

template <>
struct entt_traits<::EntityId> : ::EntityIdTraits {};

} // namespace entt::internal

// LeviLamina 26.x EntityContext has two pointer-sized reference members followed
// by EntityId. References are represented as pointers in the AArch64 ABI.
struct EntityContextMirror {
    std::uintptr_t mRegistry{};
    std::uintptr_t mEnTTRegistry{};
    EntityId mEntity{};
};

struct DebugCameraIsActiveComponent {};

static_assert(sizeof(EntityId) == sizeof(std::uint32_t));
static_assert(sizeof(EntityContextMirror) == 0x18);
static_assert(offsetof(EntityContextMirror, mEnTTRegistry) == 0x08);
static_assert(offsetof(EntityContextMirror, mEntity) == 0x10);
static_assert(
    entt::type_hash<DebugCameraIsActiveComponent>::value() == 0x25D8BF40u,
    "The compiler/EnTT type-name ABI does not match Bedrock 1.26.52.3");

namespace {

using Registry = entt::basic_registry<EntityId>;
using UpdateFn = bool (*)(void*, bool);
using GetLocalPlayerFn = void* (*)(void*);

constexpr std::string_view kMinecraftVersion = "1.26.52.3";
constexpr std::string_view kMinecraftLibrary = "libminecraftpe.so";
constexpr std::string_view kModuleId = "levi_freecam.Freecam";
constexpr std::string_view kButtonId = "levi_freecam.Freecam.Button";

// GNU Build ID from the supplied libminecraftpe-v1.26.52.3.so.xz.
constexpr std::array<std::uint8_t, 20> kBuildId{
    0x56, 0xde, 0x9e, 0xed, 0x07, 0x76, 0x31, 0xe0, 0x3a, 0x31,
    0xf4, 0xf5, 0x8e, 0xb2, 0xf0, 0x2e, 0x00, 0x07, 0x1d, 0x33};

// All addresses below are RVAs from the ELF load bias (base address). They were
// verified against the supplied 1.26.52.3 AArch64 client.
struct TargetLayout {
    static constexpr std::uintptr_t clientInstanceVtable = 0x12BC3300;
    static constexpr std::uintptr_t clientInstanceTypeInfo = 0x12BC42B8;
    static constexpr std::uintptr_t clientInstanceTypeName = 0x02BE2BEB;

    // IClientInstance Itanium vtable slots, with two destructor entries counted.
    static constexpr std::size_t updateVtableSlot = 25;
    static constexpr std::size_t localPlayerVtableSlot = 32;

    static constexpr std::uintptr_t update = 0x098036A4;
    static constexpr std::uintptr_t localPlayer = 0x09808050;

    static constexpr std::uintptr_t actorEntityContextOffset = 0x08;
};

struct TargetImage {
    std::uintptr_t base{};
    bool buildIdValid{};
};

constexpr std::size_t align4(std::size_t value) noexcept {
    return (value + 3u) & ~std::size_t{3u};
}

bool inspectBuildId(const dl_phdr_info* info, TargetImage& result) {
    if (!info || !info->dlpi_name) {
        return false;
    }

    const std::string_view path{info->dlpi_name};
    const std::size_t slash = path.find_last_of('/');
    const std::string_view basename = path.substr(slash == std::string_view::npos ? 0 : slash + 1);
    if (basename != kMinecraftLibrary) {
        return false;
    }

    result.base = static_cast<std::uintptr_t>(info->dlpi_addr);

    for (std::uint16_t index = 0; index < info->dlpi_phnum; ++index) {
        const auto& phdr = info->dlpi_phdr[index];
        if (phdr.p_type != PT_NOTE || phdr.p_filesz < sizeof(Elf64_Nhdr)) {
            continue;
        }

        const auto* noteBase = reinterpret_cast<const std::uint8_t*>(
            static_cast<std::uintptr_t>(info->dlpi_addr) + phdr.p_vaddr);
        const std::size_t limit = static_cast<std::size_t>(phdr.p_filesz);
        std::size_t offset = 0;

        while (offset + sizeof(Elf64_Nhdr) <= limit) {
            const auto* note = reinterpret_cast<const Elf64_Nhdr*>(noteBase + offset);
            offset += sizeof(Elf64_Nhdr);

            const std::size_t nameSize = align4(note->n_namesz);
            const std::size_t descSize = align4(note->n_descsz);
            if (nameSize > limit - offset || descSize > limit - offset - nameSize) {
                break;
            }

            const auto* name = noteBase + offset;
            const auto* description = name + nameSize;

            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz >= 3 &&
                std::memcmp(name, "GNU", 3) == 0 && note->n_descsz == kBuildId.size() &&
                std::memcmp(description, kBuildId.data(), kBuildId.size()) == 0) {
                result.buildIdValid = true;
                return true;
            }

            offset += nameSize + descSize;
        }
    }

    return true;
}

TargetImage findTargetImage() noexcept {
    TargetImage image{};
    dl_iterate_phdr(
        [](dl_phdr_info* info, std::size_t, void* opaque) -> int {
            auto& out = *static_cast<TargetImage*>(opaque);
            if (inspectBuildId(info, out)) {
                return out.buildIdValid ? 1 : 0;
            }
            return 0;
        },
        &image);
    return image;
}

bool validateClientInstanceVtable(const TargetImage& image) noexcept {
    if (!image.base || !image.buildIdValid) {
        return false;
    }

    auto** vtable = reinterpret_cast<void**>(image.base + TargetLayout::clientInstanceVtable);
    const auto offsetToTop = reinterpret_cast<std::uintptr_t>(vtable[-2]);
    const auto typeInfo = reinterpret_cast<std::uintptr_t>(vtable[-1]);
    if (offsetToTop != 0 || typeInfo != image.base + TargetLayout::clientInstanceTypeInfo) {
        return false;
    }

    auto* typeInfoWords = reinterpret_cast<const std::uintptr_t*>(typeInfo);
    if (typeInfoWords[1] != image.base + TargetLayout::clientInstanceTypeName) {
        return false;
    }

    const auto* name = reinterpret_cast<const char*>(image.base + TargetLayout::clientInstanceTypeName);
    if (std::strcmp(name, "14ClientInstance") != 0) {
        return false;
    }

    if (reinterpret_cast<std::uintptr_t>(vtable[TargetLayout::updateVtableSlot]) !=
        image.base + TargetLayout::update) {
        return false;
    }

    if (reinterpret_cast<std::uintptr_t>(vtable[TargetLayout::localPlayerVtableSlot]) !=
        image.base + TargetLayout::localPlayer) {
        return false;
    }

    return true;
}

EntityContextMirror* entityContextFromPlayer(void* player) noexcept {
    if (!player) {
        return nullptr;
    }
    return reinterpret_cast<EntityContextMirror*>(
        reinterpret_cast<std::uintptr_t>(player) + TargetLayout::actorEntityContextOffset);
}

Registry* registryFromPlayer(void* player) noexcept {
    const auto* context = entityContextFromPlayer(player);
    if (!context || !context->mEnTTRegistry) {
        return nullptr;
    }
    return reinterpret_cast<Registry*>(context->mEnTTRegistry);
}

constexpr std::string_view kFreecamSvg = R"svg(
<svg viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <path fill="#E0E0E0" stroke="#333333" stroke-width="3" d="M8 14h48v36H8z"/>
  <path fill="#777777" d="M14 20h36v24H14z"/>
  <circle cx="32" cy="32" r="8" fill="#222222"/>
  <path fill="#222222" d="M32 6l4 8h-8l4-8zM58 32l-8 4v-8l8 4zM32 58l-4-8h8l-4 8zM6 32l8-4v8l-8-4z"/>
</svg>
)svg";

class FreecamModImpl;
FreecamModImpl* g_mod = nullptr;
UpdateFn g_originalUpdate = nullptr;

bool updateDetour(void* self, bool isInitFinished) noexcept;

class FreecamModImpl {
public:
    static FreecamModImpl& instance() {
        static FreecamModImpl value;
        return value;
    }

    bool load() {
        mNativeMod = ll::mod::NativeMod::current();
        if (!mNativeMod) {
            return false;
        }

        auto& logger = mNativeMod->getLogger();
        logger.info("Loading Freecam {}", kMinecraftVersion);

        mTarget = findTargetImage();
        if (!mTarget.base || !mTarget.buildIdValid) {
            logger.error("Minecraft Build ID mismatch; expected 1.26.52.3 build 56de9eed...71d335");
            return false;
        }

        if (!validateClientInstanceVtable(mTarget)) {
            logger.error("ClientInstance ABI validation failed; refusing to hook");
            return false;
        }

        auto* target = reinterpret_cast<void*>(mTarget.base + TargetLayout::update);
        mUpdateHook = pl::memory::HookHandle(
            target,
            reinterpret_cast<void*>(&updateDetour),
            reinterpret_cast<void**>(&g_originalUpdate),
            pl::memory::HookPriority::High);

        if (!mUpdateHook.installed() || !g_originalUpdate) {
            logger.error("Failed to hook ClientInstance::update");
            return false;
        }

        g_mod = this;

        pl::modmenu::ModuleInfo module{};
        module.moduleId = std::string{kModuleId};
        module.displayName = "Freecam";
        module.description =
            "Native debug camera. The real player is not teleported or switched to spectator.";
        module.modId = mNativeMod->getId();
        module.defaultEnabled = true;
        module.hideInHudEditor = false;
        module.onToggle = [this](std::string_view, bool enabled) {
            setMenuEnabled(enabled);
        };

        if (!pl::modmenu::registerModule(module)) {
            logger.error("Failed to register Freecam with Mod Menu");
            g_mod = nullptr;
            mUpdateHook.reset();
            return false;
        }

        // Some ModMenu versions do not invoke onToggle for defaultEnabled. Make the
        // initial module state deterministic without relying on that detail.
        setMenuEnabled(true);
        return true;
    }

    bool enable() {
        // Native mod lifecycle enable is separate from the ModMenu Freecam toggle.
        return mNativeMod != nullptr && mUpdateHook.installed();
    }

    bool disable() {
        request(false);
        pl::modmenu::unregisterButton(kButtonId);
        mMenuEnabled.store(false, std::memory_order_release);
        return true;
    }

    bool unload() {
        request(false);
        pl::modmenu::unregisterButton(kButtonId);
        pl::modmenu::unregisterModule(kModuleId);
        mMenuEnabled.store(false, std::memory_order_release);
        g_mod = nullptr;
        mUpdateHook.reset();
        g_originalUpdate = nullptr;
        mNativeMod = nullptr;
        mTarget = {};
        mOwnedRegistry = nullptr;
        mOwnedActive = false;
        mBoundPlayer = nullptr;
        return true;
    }

    bool onClientUpdate(void* clientInstance) noexcept {
        try {
            return onClientUpdateImpl(clientInstance);
        } catch (...) {
            // Never allow an unexpected exception to cross the Minecraft hook boundary.
            // In particular, EnTT context construction/removal can allocate and therefore
            // may throw on memory pressure. Disable the request and forget ownership; the
            // registry itself remains owned by the game and will clean up its global state.
            mRequested.store(false, std::memory_order_release);
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            mBoundPlayer = nullptr;
            return false;
        }
    }

private:
    bool onClientUpdateImpl(void* clientInstance) {
        if (!clientInstance) {
            return false;
        }

        // Re-check the object vtable. This is cheap and makes a stale/foreign object
        // fail closed rather than turning an ABI mismatch into an arbitrary call.
        auto** vtable = *reinterpret_cast<void***>(clientInstance);
        if (!vtable) {
            return false;
        }

        const auto updateEntry = reinterpret_cast<std::uintptr_t>(
            vtable[TargetLayout::updateVtableSlot]);
        const auto localPlayerEntry = reinterpret_cast<std::uintptr_t>(
            vtable[TargetLayout::localPlayerVtableSlot]);
        if (updateEntry != mTarget.base + TargetLayout::update ||
            localPlayerEntry != mTarget.base + TargetLayout::localPlayer) {
            return false;
        }

        const auto requested = mRequested.load(std::memory_order_acquire);
        if (!requested && !mOwnedActive) {
            return true;
        }

        auto* getLocalPlayer = reinterpret_cast<GetLocalPlayerFn>(
            mTarget.base + TargetLayout::localPlayer);
        void* player = getLocalPlayer(clientInstance);
        Registry* registry = registryFromPlayer(player);

        if (!player) {
            if (mBoundPlayer || mOwnedRegistry) {
                mRequested.store(false, std::memory_order_release);
            }
            mBoundPlayer = nullptr;
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            return false;
        }

        if (mBoundPlayer && mBoundPlayer != player) {
            // Player replacement is treated as a world/session boundary. Do not touch
            // the old registry after replacement; it may already be destructing.
            mBoundPlayer = nullptr;
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            mRequested.store(false, std::memory_order_release);
        }

        if (mOwnedRegistry && registry != mOwnedRegistry) {
            // Dimension/world registry changed. The old global component belonged to the
            // old registry and will be destroyed with it. Freecam is auto-disabled.
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            mBoundPlayer = nullptr;
            mRequested.store(false, std::memory_order_release);
        }

        if (!registry) {
            mBoundPlayer = nullptr;
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            mRequested.store(false, std::memory_order_release);
            return false;
        }

        auto& globals = registry->ctx();
        using DebugCameraState = DebugCameraIsActiveComponent;

        if (!mRequested.load(std::memory_order_acquire)) {
            if (mOwnedActive && mOwnedRegistry == registry) {
                // Only erase state this mod inserted. If vanilla or another mod already
                // owned the component, leaving it untouched is required for correctness.
                globals.erase<DebugCameraState>();
            }
            mOwnedActive = false;
            mOwnedRegistry = nullptr;
            mBoundPlayer = nullptr;
            return true;
        }

        if (!globals.contains<DebugCameraState>()) {
            globals.emplace<DebugCameraState>();
            mOwnedActive = true;
            mOwnedRegistry = registry;
        } else {
            // Vanilla/native debug camera is already active. Do not claim ownership and
            // never remove another owner’s component when this mod is disabled.
            mOwnedActive = false;
            mOwnedRegistry = registry;
        }
        mBoundPlayer = player;
        return true;
    }

    FreecamModImpl() = default;
    ~FreecamModImpl() = default;

    void request(bool enabled) noexcept {
        mRequested.store(enabled, std::memory_order_release);
    }

    void setMenuEnabled(bool enabled) {
        mMenuEnabled.store(enabled, std::memory_order_release);
        request(false);
        pl::modmenu::unregisterButton(kButtonId);

        if (!enabled) {
            return;
        }

        pl::modmenu::ButtonInfo button{};
        button.buttonId = std::string{kButtonId};
        button.moduleId = std::string{kModuleId};
        button.displayName = "Freecam";
        button.modId = mNativeMod ? mNativeMod->getId() : std::string{};
        button.label = "Freecam";
        button.behavior = pl::modmenu::ButtonBehavior::Toggle;
        button.defaultVisible = true;
        button.stylePreset = pl::modmenu::ButtonStylePreset::Accent;
        button.widthScale = 1.0f;
        button.heightScale = 1.0f;
        button.iconFormat = pl::modmenu::ButtonIconFormat::Svg;
        button.hideLabelWhenIconPresent = false;

        const auto* svg = reinterpret_cast<const unsigned char*>(kFreecamSvg.data());
        button.iconData.assign(svg, svg + kFreecamSvg.size());

        button.onEvent = [this](std::string_view, pl::modmenu::ButtonEvent event, float value) {
            if (event != pl::modmenu::ButtonEvent::StateChanged) {
                return;
            }
            request(value > 0.5f);
        };

        if (!pl::modmenu::registerButton(button) && mNativeMod) {
            mNativeMod->getLogger().warn("Failed to register Freecam HUD button");
        }
    }

    ll::mod::NativeMod* mNativeMod{};
    TargetImage mTarget{};
    pl::memory::HookHandle mUpdateHook{};

    std::atomic_bool mMenuEnabled{false};
    std::atomic_bool mRequested{false};

    Registry* mOwnedRegistry{};
    void* mBoundPlayer{};
    bool mOwnedActive{};
};

bool updateDetour(void* self, bool isInitFinished) noexcept {
    if (g_mod) {
        try {
            (void)g_mod->onClientUpdate(self);
        } catch (...) {
            // onClientUpdate() is itself exception-safe; this guard prevents a future
            // callback/lifecycle edit from ever propagating through the native hook ABI.
        }
    }
    return g_originalUpdate
        ? g_originalUpdate(self, isInitFinished)
        : false;
}

} // namespace

export namespace levi_freecam {

class FreecamMod {
public:
    static FreecamMod& instance();
    bool load();
    bool enable();
    bool disable();
    bool unload();
};

} // namespace levi_freecam

namespace levi_freecam {

FreecamMod& FreecamMod::instance() {
    static FreecamMod value;
    return value;
}

bool FreecamMod::load() {
    return FreecamModImpl::instance().load();
}

bool FreecamMod::enable() {
    return FreecamModImpl::instance().enable();
}

bool FreecamMod::disable() {
    return FreecamModImpl::instance().disable();
}

bool FreecamMod::unload() {
    return FreecamModImpl::instance().unload();
}

} // namespace levi_freecam
