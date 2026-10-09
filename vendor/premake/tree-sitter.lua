project "tree-sitter"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    staticruntime "on"
    warnings "Off"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    -- lib.c includes every other source of the runtime; listing them as well would define each twice.
    files
    {
        "%{wks.location}/vendor/tree-sitter/lib/src/lib.c"
    }

    includedirs
    {
        "%{wks.location}/vendor/tree-sitter/lib/src",
        "%{wks.location}/vendor/tree-sitter/lib/include"
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

project "tree-sitter-c-sharp"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    staticruntime "on"
    warnings "Off"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "%{wks.location}/vendor/tree-sitter-c-sharp/src/parser.c",
        "%{wks.location}/vendor/tree-sitter-c-sharp/src/scanner.c"
    }

    includedirs
    {
        "%{wks.location}/vendor/tree-sitter-c-sharp/src"
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
