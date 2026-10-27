workspace "Graphite"
    architecture "x64"
    startproject "Graphite"
    configurations { "Debug", "Release", "Dist" }
    flags { "MultiProcessorCompile" }
    -- miniz.h otherwise #defines zlib's names (compress, crc32, ...) in every file that includes it.
    defines { "IMGUI_USE_WCHAR32", "MINIZ_NO_ZLIB_COMPATIBLE_NAMES" }

    -- The C assert() is how ImGui, nlohmann/json and pugixml check their invariants, and
    -- MSVC leaves it on unless NDEBUG is defined - premake does not define it for an
    -- optimised configuration. Without this a failed check in any of them ends the process
    -- in the build a user runs. Our own checks go through CORE_ASSERT, which keys off
    -- CORE_DEBUG and is unaffected.
    filter "configurations:Release or Dist"
        defines { "NDEBUG" }
    filter {}

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["GLFW"]    = "%{wks.location}/vendor/glfw/include"
IncludeDir["ImGui"]   = "%{wks.location}/vendor/imgui"
IncludeDir["ImAnim"]  = "%{wks.location}/vendor/imanim"
IncludeDir["ImPlot"]  = "%{wks.location}/vendor/implot"
IncludeDir["ImSpinner"] = "%{wks.location}/vendor/imspinner"
IncludeDir["doctest"] = "%{wks.location}/vendor/doctest"
IncludeDir["libgit2"]       = "%{wks.location}/vendor/libgit2/include"
IncludeDir["pugixml"]       = "%{wks.location}/vendor/pugixml/src"
IncludeDir["json"]    = "%{wks.location}/vendor/json/single_include"
IncludeDir["Icons"]   = "%{wks.location}/vendor/fonts"
IncludeDir["spdlog"]  = "%{wks.location}/vendor/spdlog/include"
IncludeDir["TreeSitter"] = "%{wks.location}/vendor/tree-sitter/lib/include"
IncludeDir["miniz"] = "%{wks.location}/vendor/miniz"
IncludeDir["MinizExport"] = "%{wks.location}/vendor/premake/miniz"
IncludeDir["stb"] = "%{wks.location}/vendor/stb"
IncludeDir["stbDeprecated"] = "%{wks.location}/vendor/stb/deprecated"

group "Dependencies"
    include "vendor/premake/glfw.lua"
    include "vendor/premake/imgui.lua"
    include "vendor/premake/libgit2.lua"
    include "vendor/premake/miniz.lua"
    include "vendor/premake/pugixml.lua"
    include "vendor/premake/stb.lua"
    include "vendor/premake/tree-sitter.lua"
group ""

group "Engine"
    include "Core"
    include "Core/tests"
group ""

group "App"
    include "Graphite"
    include "Graphite/tests"
group ""
