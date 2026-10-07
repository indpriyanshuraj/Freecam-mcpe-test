add_rules("mode.debug", "mode.release")
set_policy("package.requires_lock", true)
set_policy("build.c++.modules", true)
-- This project uses named modules but never imports the C++ standard library module.
-- Disable Xmake's automatic std-module discovery so Android NDK builds do not
-- look for libc++.modules.json, which is not shipped by the NDK toolchain.
set_policy("build.c++.modules.std", false)
set_xmakever("3.1.1")

-- BedrockTools uses the same Xmake/preloader dependency boundary.  We adopt that
-- boundary here instead of copying its project sources.
package("preloader")
    set_homepage("https://github.com/LiteLDev/preloader-android")
    set_description("Preloader Android")
    add_urls("https://github.com/LiteLDev/preloader-android.git")
    add_versions("main", "main")
    add_deps("cmake")
    on_install("android", function (package)
        import("package.tools.cmake").install(package)
    end)
package_end()

add_requires("preloader")
add_requires("entt v4.0.0")
add_requires("fmt")

target("levi_freecam")
    set_kind("shared")
    set_runtimes("c++_shared")
    set_languages("c++23")
    set_strip("all")
    add_files("src/freecam.cppm", "src/freecam/*.cppm")
    add_packages("preloader", "entt", "fmt")

    if is_plat("android") then
        add_cxflags(
            "-fPIC",
            "-Oz",
            "-fvisibility=hidden",
            "-fvisibility-inlines-hidden",
            "-fno-rtti",
            "-fexceptions",
            "-ffunction-sections",
            "-fdata-sections",
            "-w"
        )
        add_ldflags(
            "-Wl,--gc-sections",
            "-Wl,--icf=all",
            "-Wl,-z,max-page-size=16384"
        )
        add_syslinks("android", "log", "dl")
    end

    after_build(function (target)
        if not target:is_plat("android") then return end
        import("lib.detect.find_tool")
        local python = find_tool("python3") or find_tool("python")
        assert(python, "Python 3 is required to package levi_freecam.levipack")
        local output = path.join(target:targetdir(), "levi_freecam.levipack")
        os.vrunv(python.program, {
            path.join(os.projectdir(), "scripts", "package.py"),
            "--library", target:targetfile(),
            "--manifest", path.join(os.projectdir(), "manifest.json"),
            "--mod-id", "levi_freecam",
            "--output", output
        })
        print("[levi-freecam] packaged: " .. output)
        print("[levi-freecam] native library: " .. target:targetfile())
    end)
