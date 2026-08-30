-- set minimum xmake version
set_xmakever("3.0.0")

-- set project constants
set_project("fo4test")
set_version("0.0.1")
set_license("GPL-3.0-or-later")
set_languages("c++23")
set_warnings("allextra")
set_encodings("utf-8")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")

-- pull in CommonLibF4.  This gives us two things: the `commonlibf4` static
-- library target, and the `commonlibf4.plugin` rule that turns a target into
-- an F4SE plugin (x64 shared library + generated F4SEPlugin_Version struct +
-- version resource + install/package steps).
includes("extern/CommonLibF4")

-- define targets
target("fo4test", function()
    -- Consumed by the commonlibf4.plugin rule: the version below becomes the
    -- PluginVersion in F4SEPlugin_Version and the DLL's version resource, and
    -- the license string is embedded in that resource too.
    set_version("0.0.1")
    set_license("GPL-3.0-or-later")

    -- The table is read by the rule via target:extraconf("rules", ...) and
    -- expanded into res/commonlibf4-plugin.cpp.in, which declares
    -- F4SEPlugin_Version for us -- do not declare it in src/ as well.
    -- CompatibleVersions is set to F4SE::RUNTIME_LATEST (1.11.240) there.
    add_rules("commonlibf4.plugin", {
        name = "fo4test",
        author = "doodlum",
        description = "An empty F4SE plugin template built on CommonLibF4."
    })

    -- add source files
    add_files("src/**.cpp")

    -- add header files
    add_includedirs("src")
    add_headerfiles("src/**.h")

    -- set precompiled header
    set_pcxxheader("src/PCH.h")
end)
