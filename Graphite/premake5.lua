-- The application is two projects: GraphiteLib, a static library holding everything under
-- src/ except the entry point, and Graphite, the executable that adds Main.cpp and links it.
-- GraphiteTests links the same library, so a new source file is in the tests' reach the
-- moment premake globs it, with no list to keep in step. The linker pulls from a static
-- library only the objects something references, so a test binary carries the parts of the
-- application its cases call and nothing else - the reason this needs no hand-kept list of
-- which units are headless. That also means nothing in src/ may rely on a static
-- initialiser registering itself: an object nothing names is dropped from the executable
-- as well as from the tests.
--
-- Both projects compile with the same options, which this function applies.
local function GraphiteCompileOptions()
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

    -- Two warnings MSVC leaves off by default and this repository relies on. C4062: a switch
    -- over an enum, with no default, that misses an enumerator - what makes adding one a
    -- compile error at every switch that must handle it (ADRs 0216, 0225). C5038: a member
    -- initialiser list written in an order other than the declaration order (ADR 0068).
    buildoptions { "/w44062", "/w45038" }

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    includedirs
    {
        "src",
        "%{wks.location}/Core/src",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.ImPlot}",
        "%{IncludeDir.MdiIcons}"
    }

    externalincludedirs
    {
        "%{IncludeDir.pugixml}",
        "%{IncludeDir.spdlog}",
        "%{IncludeDir.json}",
        "%{IncludeDir.TreeSitter}"
    }
    externalwarnings "Off"

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
        defines { "CORE_DIST" }
        runtime "Release"
        optimize "on"
        symbols "off"

    filter {}
end

project "GraphiteLib"
    kind "StaticLib"
    GraphiteCompileOptions()

    files { "src/**.h", "src/**.cpp" }
    removefiles { "src/Main.cpp" }

project "Graphite"
    kind "ConsoleApp"
    GraphiteCompileOptions()

    debugdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")

    -- Relative to this project rather than through %{wks.location}: that token is emitted as
    -- MSBuild's $(SolutionDir), which only points at the repository root when the build goes
    -- through the solution. Building a .vcxproj directly sets it to the project's own
    -- directory, and the copy then looks for the file inside Graphite/.
    postbuildcommands
    {
        '{COPYFILE} "../THIRD_PARTY_NOTICES.txt" "%{cfg.targetdir}"'
    }

    files { "src/Main.cpp", "resources/Graphite.rc" }

    -- rc.exe resolves the icon the script names against these directories, not the script's own.
    resincludedirs { "resources", "src" }

    -- GraphiteLib, GLFW, ImGui and opengl32 are listed explicitly: a static library does
    -- not propagate its own linker inputs to a consumer. The two tree-sitter libraries and
    -- miniz are listed for the same reason.
    -- dwmapi is Core's own need — WindowFrame asks DWM for the border the reclaimed
    -- caption took with it — but it belongs here and not in Core: a static library has
    -- no link step to use an import library in, and naming it there would only merge
    -- dwmapi.lib into Core.lib, where it collides with opengl32.lib (LNK4006).
    -- bcrypt and winhttp are Core's too, for the same reason as dwmapi.
    links { "GraphiteLib", "Core", "GLFW", "ImGui", "libgit2", "pugixml", "tree-sitter", "tree-sitter-c-sharp", "miniz", "opengl32.lib", "dwmapi.lib", "ole32.lib", "shell32.lib", "ws2_32.lib", "secur32.lib", "bcrypt.lib", "winhttp.lib" }

    filter "configurations:Dist"
        kind "WindowedApp"
        entrypoint "mainCRTStartup"
    filter {}
