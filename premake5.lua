workspace "IDE"
    architecture "x64"
    startproject "IDE"
    configurations { "Debug", "Release", "Dist" }
    flags { "MultiProcessorCompile" }

outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"

IncludeDir = {}
IncludeDir["GLFW"]    = "%{wks.location}/vendor/glfw/include"
IncludeDir["ImGui"]   = "%{wks.location}/vendor/imgui"
IncludeDir["doctest"] = "%{wks.location}/vendor/doctest"

group "Dependencies"
    include "vendor/premake/glfw.lua"
    include "vendor/premake/imgui.lua"
group ""

group "Engine"
    include "Core"
    include "Core/tests"
group ""

group "App"
    include "IDE"
group ""
