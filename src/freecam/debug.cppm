module;

#include <atomic>
#include <cstdint>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

export module levi.freecam.debug;

export namespace levi_freecam::debug {

enum class Level : std::uint8_t {
    Off = 0,
    Error = 1,
    Info = 2,
    Verbose = 3,
    Trace = 4,
};

inline constexpr std::string_view kModuleId = "levi_freecam.Debug";

class Controller {
public:
    void bind(ll::mod::NativeMod* mod) noexcept { mMod = mod; }

    void setConfiguredLevel(int value) noexcept {
        if (value < 0) value = 0;
        if (value > 4) value = 4;
        mConfiguredLevel.store(static_cast<Level>(value), std::memory_order_release);
        if (mEnabled.load(std::memory_order_acquire)) {
            mLevel.store(static_cast<Level>(value), std::memory_order_release);
        }
    }

    void setEnabled(bool enabled) noexcept {
        mEnabled.store(enabled, std::memory_order_release);
        mLevel.store(enabled ? mConfiguredLevel.load(std::memory_order_acquire) : Level::Off,
                     std::memory_order_release);
    }

    [[nodiscard]] Level level() const noexcept {
        return mLevel.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool enabled() const noexcept { return level() != Level::Off; }

    void error(std::string_view message) const {
        if (allows(Level::Error) && mMod) mMod->getLogger().error("[debug] {}", message);
    }

    void info(std::string_view message) const {
        if (allows(Level::Info) && mMod) mMod->getLogger().info("[debug] {}", message);
    }

    void verbose(std::string_view message) const {
        if (allows(Level::Verbose) && mMod) mMod->getLogger().info("[debug] {}", message);
    }

    void trace(std::string_view message) const {
        if (allows(Level::Trace) && mMod) mMod->getLogger().info("[debug] {}", message);
    }

private:
    [[nodiscard]] bool allows(Level required) const noexcept {
        return static_cast<unsigned>(level()) >= static_cast<unsigned>(required);
    }

    ll::mod::NativeMod* mMod{};
    std::atomic<Level> mConfiguredLevel{Level::Info};
    std::atomic<Level> mLevel{Level::Off};
    std::atomic_bool mEnabled{false};
};

} // namespace levi_freecam::debug
