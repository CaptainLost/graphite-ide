project "Graphite"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"
    warnings "High"
    fatalwarnings { "All" }

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")
    debugdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")

    postbuildcommands
    {
        '{COPYFILE} "%{wks.location}/vendor/fonts/materialdesignicons-webfont.ttf" "%{cfg.targetdir}"',
        '{COPYFILE} "%{wks.location}/vendor/fonts/JetBrainsMono-Regular.ttf" "%{cfg.targetdir}"',
        '{COPYFILE} "%{wks.location}/vendor/fonts/JetBrainsMono-Italic.ttf" "%{cfg.targetdir}"'
    }

    files { "src/**.h", "src/**.cpp" }

    includedirs
    {
        "src",
        "%{wks.location}/Core/src",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.MdiIcons}"
    }

    -- GLFW, ImGui and opengl32 are listed explicitly: the Core static library
    -- does not propagate its own linker inputs to a consumer.
    links { "Core", "GLFW", "ImGui", "libgit2", "opengl32.lib", "ole32.lib", "shell32.lib", "ws2_32.lib", "secur32.lib" }

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
