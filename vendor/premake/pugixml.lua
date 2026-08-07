project "pugixml"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"
    warnings "Off"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    -- The whole library is one translation unit plus two headers.
    files
    {
        "%{wks.location}/vendor/pugixml/src/pugixml.cpp",
        "%{wks.location}/vendor/pugixml/src/pugixml.hpp",
        "%{wks.location}/vendor/pugixml/src/pugiconfig.hpp"
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
