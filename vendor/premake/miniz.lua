project "miniz"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    staticruntime "on"
    warnings "Off"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "%{wks.location}/vendor/miniz/miniz.c",
        "%{wks.location}/vendor/miniz/miniz_tdef.c",
        "%{wks.location}/vendor/miniz/miniz_tinfl.c",
        "%{wks.location}/vendor/miniz/miniz_zip.c"
    }

    includedirs
    {
        "%{IncludeDir.miniz}",
        "%{IncludeDir.MinizExport}"
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
