project "Core"
    kind "StaticLib"
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

    pchheader "corepch.h"
    pchsource "src/corepch.cpp"

    files { "src/**.h", "src/**.cpp" }

    includedirs
    {
        "src",
        "%{IncludeDir.GLFW}",
        "%{IncludeDir.ImGui}",
        "%{wks.location}/vendor/imgui/backends"
    }

    externalincludedirs
    {
        "%{IncludeDir.libgit2}",
        "%{IncludeDir.miniz}",
        "%{IncludeDir.MinizExport}",
        "%{IncludeDir.ImAnim}",
        "%{IncludeDir.ImPlot}",
        "%{IncludeDir.ImSpinner}",
        "%{IncludeDir.json}",
        "%{IncludeDir.MdiIcons}",
        "%{IncludeDir.spdlog}"
    }
    externalwarnings "Off"

    links { "GLFW", "ImGui", "opengl32.lib" }

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
