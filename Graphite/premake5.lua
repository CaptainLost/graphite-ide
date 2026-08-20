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
    -- The clangd copy is a plain cmd `if exist` guard rather than {COPYFILE}: the binary is
    -- ~100MB, downloaded rather than built (see vendor/bin/clangd/README.md), and is not
    -- committed to this repository, so it is absent on every machine until someone places it.
    -- {COPYFILE} would fail the build in that — currently universal — case; the guard makes a
    -- missing server binary the degraded-but-working state ADR 0488 intends, not a build error.
    -- The theme copies are guarded by `if not exist` for a different reason: the themes
    -- directory beside the executable is also where the settings panel saves a user's own
    -- edits, and a shipped gruvbox file is an ordinary editable scheme, not a read-only
    -- built-in — copying it outright on every build would overwrite an edit made through
    -- the settings panel.
    postbuildcommands
    {
        '{COPYFILE} "../vendor/fonts/materialdesignicons-webfont.ttf" "%{cfg.targetdir}"',
        '{COPYFILE} "../vendor/fonts/JetBrainsMono-Regular.ttf" "%{cfg.targetdir}"',
        'if exist "..\\vendor\\bin\\clangd\\clangd.exe" copy /Y "..\\vendor\\bin\\clangd\\clangd.exe" "%{cfg.targetdir}"',
        'if not exist "%{cfg.targetdir}\\themes" mkdir "%{cfg.targetdir}\\themes"',
        'if not exist "%{cfg.targetdir}\\themes\\gruvbox-dark-hard.json" copy /Y "themes\\gruvbox-dark-hard.json" "%{cfg.targetdir}\\themes"',
        'if not exist "%{cfg.targetdir}\\themes\\gruvbox-dark-medium.json" copy /Y "themes\\gruvbox-dark-medium.json" "%{cfg.targetdir}\\themes"',
        'if not exist "%{cfg.targetdir}\\themes\\gruvbox-dark-soft.json" copy /Y "themes\\gruvbox-dark-soft.json" "%{cfg.targetdir}\\themes"'
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
        "%{IncludeDir.spdlog}",
        "%{IncludeDir.json}"
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
