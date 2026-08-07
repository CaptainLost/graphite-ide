project "CoreTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"
    warnings "High"
    fatalwarnings { "All" }

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files { "src/**.h", "src/**.cpp" }

    includedirs
    {
        "%{wks.location}/Core/src"
    }

    externalincludedirs
    {
        "%{IncludeDir.doctest}",
        "%{IncludeDir.libgit2}",
        "%{IncludeDir.pugixml}"
    }

    externalwarnings "Off"

    links { "Core", "GLFW", "ImGui", "libgit2", "pugixml", "opengl32.lib", "ole32.lib", "shell32.lib", "ws2_32.lib", "secur32.lib" }

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
