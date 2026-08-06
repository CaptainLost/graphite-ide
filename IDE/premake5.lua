project "IDE"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"
    warnings "High"
    fatalwarnings { "All" }

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")

    files { "src/**.h", "src/**.cpp" }

    includedirs
    {
        "src",
        "%{wks.location}/Core/src",
        "%{IncludeDir.ImGui}"
    }

    -- GLFW, ImGui i opengl32 sa wymienione jawnie: biblioteka statyczna Core
    -- nie przenosi na konsumenta swoich wejsc linkera.
    links { "Core", "GLFW", "ImGui", "opengl32.lib" }

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
        defines { "CORE_DIST" }
        runtime "Release"
        optimize "on"
        symbols "off"
