module;

#include <array>
#include <cstdint>
#include <cstring>
#include <link.h>
#include <string_view>

export module levi.freecam.target;

export namespace levi_freecam::target {

inline constexpr std::string_view kMinecraftVersion = "1.26.52.3";
inline constexpr std::string_view kMinecraftLibrary = "libminecraftpe.so";

inline constexpr std::array<std::uint8_t, 20> kBuildId{
    0x56, 0xde, 0x9e, 0xed, 0x07, 0x76, 0x31, 0xe0, 0x3a, 0x31,
    0xf4, 0xf5, 0x8e, 0xb2, 0xf0, 0x2e, 0x00, 0x07, 0x1d, 0x33};

struct Layout {
    static constexpr std::uintptr_t clientInstanceVtable = 0x12BC3300;
    static constexpr std::uintptr_t clientInstanceTypeInfo = 0x12BC42B8;
    static constexpr std::uintptr_t clientInstanceTypeName = 0x02BE2BEB;
    static constexpr std::size_t updateVtableSlot = 25;
    static constexpr std::size_t localPlayerVtableSlot = 32;
    static constexpr std::uintptr_t update = 0x098036A4;
    static constexpr std::uintptr_t localPlayer = 0x09808050;
    static constexpr std::uintptr_t actorEntityContextOffset = 0x08;
};

struct Image {
    std::uintptr_t base{};
    bool buildIdValid{};
};

constexpr std::size_t align4(std::size_t value) noexcept {
    return (value + 3u) & ~std::size_t{3u};
}

inline bool inspectBuildId(const dl_phdr_info* info, Image& result) {
    if (!info || !info->dlpi_name) return false;
    const std::string_view path{info->dlpi_name};
    const auto slash = path.find_last_of('/');
    const auto basename = path.substr(slash == std::string_view::npos ? 0 : slash + 1);
    if (basename != kMinecraftLibrary) return false;

    result.base = static_cast<std::uintptr_t>(info->dlpi_addr);
    for (std::uint16_t index = 0; index < info->dlpi_phnum; ++index) {
        const auto& phdr = info->dlpi_phdr[index];
        if (phdr.p_type != PT_NOTE || phdr.p_filesz < sizeof(Elf64_Nhdr)) continue;
        const auto* noteBase = reinterpret_cast<const std::uint8_t*>(
            static_cast<std::uintptr_t>(info->dlpi_addr) + phdr.p_vaddr);
        const auto limit = static_cast<std::size_t>(phdr.p_filesz);
        std::size_t offset = 0;
        while (offset + sizeof(Elf64_Nhdr) <= limit) {
            const auto* note = reinterpret_cast<const Elf64_Nhdr*>(noteBase + offset);
            offset += sizeof(Elf64_Nhdr);
            const auto nameSize = align4(note->n_namesz);
            const auto descSize = align4(note->n_descsz);
            if (nameSize > limit - offset || descSize > limit - offset - nameSize) break;
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

inline Image find() noexcept {
    Image image{};
    dl_iterate_phdr([](dl_phdr_info* info, std::size_t, void* opaque) -> int {
        auto& out = *static_cast<Image*>(opaque);
        if (inspectBuildId(info, out)) return out.buildIdValid ? 1 : 0;
        return 0;
    }, &image);
    return image;
}

inline bool validateClientInstanceVtable(const Image& image) noexcept {
    if (!image.base || !image.buildIdValid) return false;
    auto** vtable = reinterpret_cast<void**>(image.base + Layout::clientInstanceVtable);
    if (reinterpret_cast<std::uintptr_t>(vtable[-2]) != 0) return false;
    if (reinterpret_cast<std::uintptr_t>(vtable[-1]) != image.base + Layout::clientInstanceTypeInfo) return false;
    const auto* typeInfo = reinterpret_cast<const std::uintptr_t*>(image.base + Layout::clientInstanceTypeInfo);
    if (typeInfo[1] != image.base + Layout::clientInstanceTypeName) return false;
    const auto* name = reinterpret_cast<const char*>(image.base + Layout::clientInstanceTypeName);
    if (std::strcmp(name, "14ClientInstance") != 0) return false;
    if (reinterpret_cast<std::uintptr_t>(vtable[Layout::updateVtableSlot]) != image.base + Layout::update) return false;
    if (reinterpret_cast<std::uintptr_t>(vtable[Layout::localPlayerVtableSlot]) != image.base + Layout::localPlayer) return false;
    return true;
}

} // namespace levi_freecam::target
