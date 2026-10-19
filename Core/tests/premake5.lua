project "CoreTests"
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

    -- Two warnings MSVC leaves off by default and this repository relies on. C4062: a switch
    -- over an enum, with no default, that misses an enumerator - what makes adding one a
    -- compile error at every switch that must handle it (ADRs 0216, 0225). C5038: a member
    -- initialiser list written in an order other than the declaration order (ADR 0068).
    buildoptions { "/w44062", "/w45038" }

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files { "src/**.h", "src/**.cpp" }

    includedirs
    {
        "%{wks.location}/Core/src",
        -- Shared fixtures such as TempDirectory.h stay at the root of src/ while the
        -- tests mirror the source tree, so every depth reaches them by bare name.
        "src",
        -- Fixtures both test projects use, such as ZipFixtures.h.
        "fixtures"
    }

    externalincludedirs
    {
        "%{IncludeDir.doctest}",
        "%{IncludeDir.libgit2}",
        "%{IncludeDir.miniz}",
        "%{IncludeDir.MinizExport}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.ImPlot}",
        "%{IncludeDir.Icons}",
        "%{IncludeDir.spdlog}"
    }

    externalwarnings "Off"

    links { "Core", "GLFW", "ImGui", "libgit2", "miniz", "opengl32.lib", "dwmapi.lib", "ole32.lib", "shell32.lib", "ws2_32.lib", "secur32.lib", "bcrypt.lib", "winhttp.lib" }

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

    filter "configurations:Dist"
        defines { "CORE_DIST" }
        runtime "Release"
        optimize "on"
