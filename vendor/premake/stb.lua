project "stb"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    staticruntime "on"
    warnings "Off"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "%{wks.location}/vendor/premake/stb/StbImplementation.c"
    }

    includedirs
    {
        "%{IncludeDir.stb}",
        "%{IncludeDir.stbDeprecated}"
    }

    filter "system:windows"
        systemversion "latest"

    filter "configurations:Debug"
        runtime "Debug"
        symbols "on"

    filter "configurations:Release"
        runtime "Release"
        optimize "on"

    filter "configurations:Dist"
        runtime "Release"
        optimize "on"
        symbols "off"

    filter {}
