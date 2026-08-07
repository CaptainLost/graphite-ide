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
IncludeDir["libgit2"]       = "%{wks.location}/vendor/libgit2/include"
IncludeDir["libgit2Config"] = "%{wks.location}/vendor/libgit2-config"

group "Dependencies"
    include "vendor/premake/glfw.lua"
    include "vendor/premake/imgui.lua"
    include "vendor/premake/libgit2.lua"
group ""

group "Engine"
    include "Core"
    include "Core/tests"
group ""

group "App"
    include "IDE"
group ""
