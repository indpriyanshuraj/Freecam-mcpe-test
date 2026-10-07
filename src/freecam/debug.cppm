module;

#include <atomic>
#include <cstdint>
#include <string_view>

#include <android/log.h>

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
inline constexpr const char* kLogTag = "LeviFreecam";

class Controller {
public:
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

    void error(std::string_view message) const noexcept {
        if (!allows(Level::Error)) return;
        emit(ANDROID_LOG_ERROR, message);
    }

    void info(std::string_view message) const noexcept {
        if (!allows(Level::Info)) return;
        emit(ANDROID_LOG_INFO, message);
    }

    void verbose(std::string_view message) const noexcept {
        if (!allows(Level::Verbose)) return;
        emit(ANDROID_LOG_DEBUG, message);
    }

    void trace(std::string_view message) const noexcept {
        if (!allows(Level::Trace)) return;
        emit(ANDROID_LOG_VERBOSE, message);
    }

    void always(std::string_view message) const noexcept {
        emit(ANDROID_LOG_INFO, message);
    }

    void alwaysError(std::string_view message) const noexcept {
        emit(ANDROID_LOG_ERROR, message);
    }

private:
    void emit(int priority, std::string_view message) const noexcept {
        __android_log_print(priority, kLogTag, "%.*s", static_cast<int>(message.size()), message.data());
    }

    [[nodiscard]] bool allows(Level required) const noexcept {
        return static_cast<unsigned>(level()) >= static_cast<unsigned>(required);
    }

    std::atomic<Level> mConfiguredLevel{Level::Info};
    std::atomic<Level> mLevel{Level::Off};
    std::atomic_bool mEnabled{false};
};

} // namespace levi_freecam::debug
