project "Graphite"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"
    warnings "High"
    fatalwarnings { "All" }

    -- Sources are UTF-8 and carry no BOM, so MSVC would otherwise read them through the
    -- machine's ANSI codepage: an em dash in a message the user reads becomes mojibake on
    -- a machine whose codepage differs from this one, or trips C4819 - which
    -- fatalwarnings turns into a build failure. This sets both the source and the
    -- execution charset to UTF-8, which is what every std::string in this codebase
    -- already claims to be.
    buildoptions { "/utf-8" }

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")

    -- Relative to this project rather than through %{wks.location}: that token is emitted as
    -- MSBuild's $(SolutionDir), which only points at the repository root when the build goes
    -- through the solution. Building a .vcxproj directly sets it to the project's own
    -- directory, and the copy then looks for the fonts inside Graphite/.
    postbuildcommands
    {
        '{COPYFILE} "../vendor/fonts/materialdesignicons-webfont.ttf" "%{cfg.targetdir}"',
        '{COPYFILE} "../vendor/fonts/JetBrainsMono-Regular.ttf" "%{cfg.targetdir}"'
    }

    files { "src/**.h", "src/**.cpp" }

    includedirs
    {
        "src",
        "%{wks.location}/Core/src",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.MdiIcons}"
    }

    externalincludedirs
    {
        "%{IncludeDir.pugixml}",
        "%{IncludeDir.spdlog}"
    }
    externalwarnings "Off"

    -- GLFW, ImGui and opengl32 are listed explicitly: the Core static library
    -- does not propagate its own linker inputs to a consumer.
    -- dwmapi is Core's own need — WindowFrame asks DWM for the border the reclaimed
    -- caption took with it — but it belongs here and not in Core: a static library has
    -- no link step to use an import library in, and naming it there would only merge
    -- dwmapi.lib into Core.lib, where it collides with opengl32.lib (LNK4006).
    links { "Core", "GLFW", "ImGui", "libgit2", "pugixml", "opengl32.lib", "dwmapi.lib", "ole32.lib", "shell32.lib", "ws2_32.lib", "secur32.lib" }

    filter "system:windows"
        systemversion "latest"
        defines { "CORE_PLATFORM_WINDOWS", "NOMINMAX", "WIN32_LEAN_AND_MEAN" }

    filter "configurations:Debug"
        defines { "CORE_DEBUG" }
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        defines { "CORE_RELEASE" }
        runtime "Release"
        optimize "on"
        symbols "on"

    filter "configurations:Dist"
        kind "WindowedApp"
        entrypoint "mainCRTStartup"
        defines { "CORE_DIST" }
        runtime "Release"
        optimize "on"
        symbols "off"
