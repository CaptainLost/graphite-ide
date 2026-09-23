workspace "Graphite"
    architecture "x64"
    startproject "Graphite"
    configurations { "Debug", "Release", "Dist" }
    flags { "MultiProcessorCompile" }
    defines { "IMGUI_USE_WCHAR32" }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["GLFW"]    = "%{wks.location}/vendor/glfw/include"
IncludeDir["ImGui"]   = "%{wks.location}/vendor/imgui"
IncludeDir["ImAnim"]  = "%{wks.location}/vendor/imanim"
IncludeDir["ImPlot"]  = "%{wks.location}/vendor/implot"
IncludeDir["doctest"] = "%{wks.location}/vendor/doctest"
IncludeDir["libgit2"]       = "%{wks.location}/vendor/libgit2/include"
IncludeDir["pugixml"]       = "%{wks.location}/vendor/pugixml/src"
IncludeDir["json"]    = "%{wks.location}/vendor/json/single_include"
IncludeDir["MdiIcons"] = "%{wks.location}/vendor/fonts"
IncludeDir["spdlog"]  = "%{wks.location}/vendor/spdlog/include"

group "Dependencies"
    include "vendor/premake/glfw.lua"
    include "vendor/premake/imgui.lua"
    include "vendor/premake/libgit2.lua"
    include "vendor/premake/pugixml.lua"
group ""

group "Engine"
    include "Core"
    include "Core/tests"
group ""

group "App"
    include "Graphite"
    include "Graphite/tests"
group ""
