module;

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/memory/Hook.hpp>

export module levi.freecam;

import levi.freecam.debug;
import levi.freecam.target;
import levi.freecam.ecs;

namespace {

using UpdateFn = bool (*)(void*, bool);
using GetLocalPlayerFn = void* (*)(void*);

constexpr std::string_view kModuleId = "levi_freecam.Freecam";
constexpr std::string_view kButtonId = "levi_freecam.Freecam.Button";
constexpr std::string_view kDebugModuleId = levi_freecam::debug::kModuleId;

constexpr std::string_view kFreecamSvg = R"svg(
<svg viewBox="0 0 64 64" xmlns="http://www.w3.org/2000/svg">
  <rect x="6" y="6" width="52" height="52" rx="5" fill="none" stroke="#FFFFFF" stroke-width="4"/>
  <path fill="#FFFFFF" d="M21 17h25v6H27v8h17v6H27v10h-6V17z"/>
</svg>
)svg";

constexpr std::string_view kFreecamActiveSvg = kFreecamSvg;

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
        if (!mNativeMod) return false;
        g_mod = this;
        mDebug.bind(mNativeMod);
        mNativeMod->getLogger().info("Freecam {} loaded; waiting for Minecraft", levi_freecam::target::kMinecraftVersion);
        return true;
    }

    bool enable() {
        if (!mNativeMod) return false;
        auto& logger = mNativeMod->getLogger();

        pl::modmenu::ModuleInfo debugModule{};
        debugModule.moduleId = std::string{kDebugModuleId};
        debugModule.displayName = "Freecam Debug";
        debugModule.description = "Diagnostic logging for hook, lifecycle, player, ECS and camera state.";
        debugModule.modId = mNativeMod->getId();
        debugModule.defaultEnabled = false;
        debugModule.configs.push_back(pl::modmenu::ConfigEntry{
            .key = "level",
            .displayName = "Debug Level",
            .type = pl::modmenu::ConfigType::SliderInt,
            .defaultValue = "2",
            .minValue = "0",
            .maxValue = "4",
        });
        debugModule.onToggle = [this](std::string_view, bool enabled) {
            mDebug.setEnabled(enabled);
            if (mNativeMod) mNativeMod->getLogger().info("Freecam debug module: {}", enabled ? "ON" : "OFF");
        };
        debugModule.onConfigChanged = [this](std::string_view, std::string_view key, std::string_view value) {
            if (key != "level") return;
            int level = 2;
            try { level = std::stoi(std::string{value}); } catch (...) {}
            mDebug.setConfiguredLevel(level);
            if (mNativeMod) mNativeMod->getLogger().info("Freecam debug level = {}", level);
        };
        if (!pl::modmenu::registerModule(debugModule)) {
            logger.error("Freecam Debug module registration failed");
            return false;
        }

        pl::modmenu::ModuleInfo module{};
        module.moduleId = std::string{kModuleId};
        module.displayName = "Freecam";
        module.description = "Native debug camera. The real player is not teleported or switched to spectator.";
        module.modId = mNativeMod->getId();
        module.defaultEnabled = true;
        module.configs.push_back(pl::modmenu::ConfigEntry{
            .key = "enabled",
            .displayName = "Enabled",
            .type = pl::modmenu::ConfigType::Toggle,
            .defaultValue = "true",
        });
        module.onToggle = [this](std::string_view, bool enabled) { setMenuEnabled(enabled); };
        module.onConfigChanged = [this](std::string_view, std::string_view key, std::string_view value) {
            if (key != "enabled") return;
            const bool enabled = value == "true" || value == "1";
            mConfigEnabled.store(enabled, std::memory_order_release);
            request(mMenuEnabled.load(std::memory_order_acquire) && enabled);
            if (mNativeMod) mNativeMod->getLogger().info("Freecam setting: enabled = {}", enabled);
        };
        if (!pl::modmenu::registerModule(module)) {
            logger.error("Freecam Mod Menu registration failed");
            pl::modmenu::unregisterModule(kDebugModuleId);
            return false;
        }

        setMenuEnabled(true);
        startRuntimeWatcher();
        logger.info("Freecam enabled; waiting for ClientInstance::update hook");
        mDebug.info("Lifecycle enable complete; runtime watcher started");
        return true;
    }

    bool disable() {
        request(false);
        pl::modmenu::unregisterButton(kButtonId);
        pl::modmenu::unregisterModule(kDebugModuleId);
        pl::modmenu::unregisterModule(kModuleId);
        mMenuEnabled.store(false, std::memory_order_release);
        mDebug.setEnabled(false);
        return true;
    }

    bool unload() {
        request(false);
        stopRuntimeWatcher();
        pl::modmenu::unregisterButton(kButtonId);
        pl::modmenu::unregisterModule(kDebugModuleId);
        pl::modmenu::unregisterModule(kModuleId);
        mMenuEnabled.store(false, std::memory_order_release);
        mDebug.setEnabled(false);
        g_mod = nullptr;
        mUpdateHook.reset();
        g_originalUpdate = nullptr;
        mNativeMod = nullptr;
        mTarget = {};
        mOwnedRegistry = nullptr;
        mLastRegistry = nullptr;
        mOwnedActive = false;
        mBoundPlayer = nullptr;
        return true;
    }

    bool onClientUpdate(void* clientInstance) noexcept {
        try { return onClientUpdateImpl(clientInstance); }
        catch (...) {
            mRequested.store(false, std::memory_order_release);
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            mBoundPlayer = nullptr;
            mDebug.error("Exception escaped update processing; request was disabled");
            return false;
        }
    }

private:
    void startRuntimeWatcher() {
        bool expected = false;
        if (!mWatcherRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
        mStopWatcher.store(false, std::memory_order_release);
        mWatcher = std::thread([this] {
            mDebug.verbose("Runtime watcher started");
            for (;;) {
                if (mStopWatcher.load(std::memory_order_acquire)) return;
                if (installRuntimeHook()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    bool installRuntimeHook() {
        if (mUpdateHook.installed()) return true;
        mTarget = levi_freecam::target::find();
        if (!mTarget.base) {
            mDebug.trace("Target library not present yet");
            return false;
        }
        if (!mTarget.buildIdValid) {
            if (!mLoggedBuildMismatch.exchange(true, std::memory_order_acq_rel)) {
                mDebug.error("Minecraft Build ID mismatch; expected 1.26.52.3");
            }
            return false;
        }
        mDebug.verbose("Minecraft Build ID matched 1.26.52.3");
        mDebug.verbose("Target image base = " + std::to_string(mTarget.base));
        if (!levi_freecam::target::validateClientInstanceVtable(mTarget)) {
            if (!mLoggedAbiMismatch.exchange(true, std::memory_order_acq_rel)) {
                mDebug.error("ClientInstance ABI validation failed; hook skipped");
            }
            return false;
        }
        mDebug.verbose("ClientInstance RTTI/vtable validation passed");
        mDebug.verbose("ClientInstance::update RVA = " + std::to_string(levi_freecam::target::Layout::update));

        auto* target = reinterpret_cast<void*>(mTarget.base + levi_freecam::target::Layout::update);
        mDebug.verbose("Installing ClientInstance::update hook");
        mUpdateHook = pl::memory::HookHandle(
            target, reinterpret_cast<void*>(&updateDetour), reinterpret_cast<void**>(&g_originalUpdate),
            pl::memory::HookPriority::High);
        if (!mUpdateHook.installed() || !g_originalUpdate) {
            if (!mLoggedHookFailure.exchange(true, std::memory_order_acq_rel)) {
                mDebug.error("ClientInstance::update hook installation failed");
            }
            mUpdateHook.reset();
            g_originalUpdate = nullptr;
            return false;
        }
        mDebug.info("ClientInstance::update hook installed");
        return true;
    }

    void stopRuntimeWatcher() {
        mStopWatcher.store(true, std::memory_order_release);
        if (mWatcher.joinable()) mWatcher.join();
        mWatcherRunning.store(false, std::memory_order_release);
        mDebug.verbose("Runtime watcher stopped");
    }

    bool onClientUpdateImpl(void* clientInstance) {
        ++mUpdateCount;
        mDebug.trace("update begin");
        if (!clientInstance) {
            mDebug.verbose("update skipped: ClientInstance is null");
            return false;
        }

        auto** vtable = *reinterpret_cast<void***>(clientInstance);
        if (!vtable) {
            mDebug.error("update skipped: ClientInstance vtable is null");
            return false;
        }
        const auto updateEntry = reinterpret_cast<std::uintptr_t>(vtable[levi_freecam::target::Layout::updateVtableSlot]);
        const auto localPlayerEntry = reinterpret_cast<std::uintptr_t>(vtable[levi_freecam::target::Layout::localPlayerVtableSlot]);
        if (updateEntry != mTarget.base + levi_freecam::target::Layout::update ||
            localPlayerEntry != mTarget.base + levi_freecam::target::Layout::localPlayer) {
            mDebug.error("update skipped: live ClientInstance vtable does not match target ABI");
            return false;
        }

        const bool requested = mRequested.load(std::memory_order_acquire);
        if (!requested && !mOwnedActive) {
            mDebug.trace("update: freecam idle");
            return true;
        }

        mDebug.verbose("update: request active; resolving local player");
        auto* getLocalPlayer = reinterpret_cast<GetLocalPlayerFn>(mTarget.base + levi_freecam::target::Layout::localPlayer);
        void* player = getLocalPlayer(clientInstance);
        if (!player) {
            mDebug.verbose("update: local player unavailable; disabling request");
            mRequested.store(false, std::memory_order_release);
            mBoundPlayer = nullptr;
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            return false;
        }

        if (mBoundPlayer && mBoundPlayer != player) {
            mDebug.info("update: local player changed; rebinding disabled until next request");
            mBoundPlayer = nullptr;
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            mRequested.store(false, std::memory_order_release);
        }

        mDebug.verbose("update: local player pointer = " + std::to_string(reinterpret_cast<std::uintptr_t>(player)));
        auto* registry = levi_freecam::ecs::registryFromPlayer(player, levi_freecam::target::Layout::actorEntityContextOffset);
        if (mOwnedRegistry && registry != mOwnedRegistry) {
            mDebug.info("update: ECS registry changed; dropping old ownership");
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            mBoundPlayer = nullptr;
            mRequested.store(false, std::memory_order_release);
        }
        if (!registry) {
            mDebug.error("update: local player ECS registry is null");
            mBoundPlayer = nullptr;
            mOwnedRegistry = nullptr;
            mOwnedActive = false;
            mRequested.store(false, std::memory_order_release);
            return false;
        }

        if (mBoundPlayer != player || mLastRegistry != registry) {
            mDebug.info("update: bound to local-player ECS registry");
            mDebug.verbose("update: registry pointer = " + std::to_string(reinterpret_cast<std::uintptr_t>(registry)));
            mLastRegistry = registry;
        }

        auto& globals = registry->ctx();
        using DebugCameraState = DebugCameraIsActiveComponent;

        if (!mRequested.load(std::memory_order_acquire)) {
            if (mOwnedActive && mOwnedRegistry == registry) {
                mDebug.info("update: erasing mod-owned DebugCameraIsActiveComponent");
                globals.erase<DebugCameraState>();
            }
            mOwnedActive = false;
            mOwnedRegistry = nullptr;
            mBoundPlayer = nullptr;
            return true;
        }

        const bool alreadyActive = globals.contains<DebugCameraState>();
        mDebug.verbose(alreadyActive
            ? "update: DebugCameraIsActiveComponent already exists"
            : "update: DebugCameraIsActiveComponent absent; emplacing");
        if (!alreadyActive) {
            globals.emplace<DebugCameraState>();
            mOwnedActive = true;
            mOwnedRegistry = registry;
            mDebug.info("update: debug-camera global state enabled");
        } else {
            mOwnedActive = false;
            mOwnedRegistry = registry;
            mDebug.trace("update: debug-camera state already exists");
        }
        mBoundPlayer = player;
        mDebug.trace("update end: ECS state processed");
        return true;
    }

    void request(bool enabled) noexcept { mRequested.store(enabled, std::memory_order_release); }

    void setMenuEnabled(bool enabled) {
        mMenuEnabled.store(enabled, std::memory_order_release);
        request(enabled && mConfigEnabled.load(std::memory_order_acquire));
        pl::modmenu::unregisterButton(kButtonId);
        if (!enabled) {
            if (mNativeMod) mNativeMod->getLogger().info("Freecam module disabled");
            return;
        }
        if (mNativeMod) mNativeMod->getLogger().info("Freecam module enabled; setting = {}", mConfigEnabled.load());

        pl::modmenu::ButtonInfo button{};
        button.buttonId = std::string{kButtonId};
        button.moduleId = std::string{kModuleId};
        button.displayName = "Freecam";
        button.modId = mNativeMod ? mNativeMod->getId() : std::string{};
        button.label.clear();
        button.behavior = pl::modmenu::ButtonBehavior::Toggle;
        button.defaultVisible = true;
        button.stylePreset = pl::modmenu::ButtonStylePreset::Accent;
        button.widthScale = 1.0f;
        button.heightScale = 1.0f;
        button.iconFormat = pl::modmenu::ButtonIconFormat::Svg;
        button.hideLabelWhenIconPresent = true;
        button.iconData.assign(reinterpret_cast<const unsigned char*>(kFreecamSvg.data()),
                               reinterpret_cast<const unsigned char*>(kFreecamSvg.data()) + kFreecamSvg.size());
        button.activeIconData.assign(reinterpret_cast<const unsigned char*>(kFreecamActiveSvg.data()),
                                     reinterpret_cast<const unsigned char*>(kFreecamActiveSvg.data()) + kFreecamActiveSvg.size());
        button.onEvent = [this](std::string_view, pl::modmenu::ButtonEvent event, float value) {
            if (event != pl::modmenu::ButtonEvent::StateChanged) return;
            const bool enabledNow = value > 0.5f;
            request(enabledNow && mConfigEnabled.load(std::memory_order_acquire));
            if (mNativeMod) mNativeMod->getLogger().info("Freecam button: {}", enabledNow ? "ON" : "OFF");
            mDebug.verbose(enabledNow ? "HUD button requested ON" : "HUD button requested OFF");
        };
        if (!pl::modmenu::registerButton(button) && mNativeMod) mNativeMod->getLogger().warn("Failed to register Freecam HUD button");
    }

    ll::mod::NativeMod* mNativeMod{};
    levi_freecam::target::Image mTarget{};
    pl::memory::HookHandle mUpdateHook{};
    std::thread mWatcher{};
    std::atomic_bool mWatcherRunning{false};
    std::atomic_bool mStopWatcher{false};
    std::atomic_bool mLoggedBuildMismatch{false};
    std::atomic_bool mLoggedAbiMismatch{false};
    std::atomic_bool mLoggedHookFailure{false};
    std::atomic_bool mMenuEnabled{false};
    std::atomic_bool mConfigEnabled{true};
    std::atomic_bool mRequested{false};
    std::uint64_t mUpdateCount{};
    levi_freecam::ecs::Registry* mOwnedRegistry{};
    levi_freecam::ecs::Registry* mLastRegistry{};
    void* mBoundPlayer{};
    bool mOwnedActive{};
    levi_freecam::debug::Controller mDebug{};
};

bool updateDetour(void* self, bool isInitFinished) noexcept {
    if (g_mod) (void)g_mod->onClientUpdate(self);
    return g_originalUpdate ? g_originalUpdate(self, isInitFinished) : false;
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
}

namespace levi_freecam {
FreecamMod& FreecamMod::instance() { static FreecamMod value; return value; }
bool FreecamMod::load() { return FreecamModImpl::instance().load(); }
bool FreecamMod::enable() { return FreecamModImpl::instance().enable(); }
bool FreecamMod::disable() { return FreecamModImpl::instance().disable(); }
bool FreecamMod::unload() { return FreecamModImpl::instance().unload(); }
}

PL_REGISTER_MOD(levi_freecam::FreecamMod, levi_freecam::FreecamMod::instance());
