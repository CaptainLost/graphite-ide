project "GraphiteTests"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"
    warnings "High"
    fatalwarnings { "All" }

    -- Sources are UTF-8 and carry no BOM, so MSVC would otherwise read them through the
    -- machine's ANSI codepage: an em dash in a message the user reads becomes mojibake on
    -- a machine whose codepage differs from this one, or trips C4819 - which
    -- fatalwarnings turns into a build failure. This sets both the source and the
    -- execution charset to UTF-8, which is what every std::string in this codebase
    -- already claims to be.
    buildoptions { "/utf-8" }

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    -- The application is an executable, so there is no library to link the code under
    -- test out of. The modules being tested are compiled into this project instead,
    -- which is why the file list reaches into Graphite's own sources.
    files
    {
        "src/**.h",
        "src/**.cpp",
        "%{wks.location}/Graphite/src/Sln/**.h",
        "%{wks.location}/Graphite/src/Sln/**.cpp",
        "%{wks.location}/Graphite/src/Lang/**.h",
        "%{wks.location}/Graphite/src/Lang/**.cpp",
        "%{wks.location}/Graphite/src/Settings/**.h",
        "%{wks.location}/Graphite/src/Settings/**.cpp",
        -- Editor is named file by file rather than globbed: most of it is widget code that
        -- drags in ImGui and the application's own layers. The units named here are the parts
        -- that are only logic, drawing nothing and reaching no further than Core and the
        -- search modules already listed below.
        "%{wks.location}/Graphite/src/Editor/AnalysisCache.h",
        "%{wks.location}/Graphite/src/Editor/AnalysisCache.cpp",
        "%{wks.location}/Graphite/src/Editor/BracketMatch.h",
        "%{wks.location}/Graphite/src/Editor/BracketMatch.cpp",
        "%{wks.location}/Graphite/src/Editor/DefinitionNavigation.h",
        "%{wks.location}/Graphite/src/Editor/DefinitionNavigation.cpp",
        "%{wks.location}/Graphite/src/Editor/EditorSettings.h",
        "%{wks.location}/Graphite/src/Editor/EditorSettings.cpp",
        "%{wks.location}/Graphite/src/Editor/FindNavigation.h",
        "%{wks.location}/Graphite/src/Editor/FindNavigation.cpp",
        "%{wks.location}/Graphite/src/Editor/PreviewSemantics.h",
        "%{wks.location}/Graphite/src/Editor/PreviewSemantics.cpp",
        "%{wks.location}/Graphite/src/Editor/LensModel.h",
        "%{wks.location}/Graphite/src/Editor/LensModel.cpp",
        "%{wks.location}/Graphite/src/Editor/SemanticOverlay.h",
        "%{wks.location}/Graphite/src/Editor/SemanticOverlay.cpp",
        "%{wks.location}/Graphite/src/Editor/LensRows.h",
        "%{wks.location}/Graphite/src/Editor/LensRows.cpp",
        "%{wks.location}/Graphite/src/Editor/MatchColumns.h",
        "%{wks.location}/Graphite/src/Editor/MatchColumns.cpp",
        "%{wks.location}/Graphite/src/Editor/ServerStatusText.h",
        "%{wks.location}/Graphite/src/Editor/ServerStatusText.cpp",
        -- Palettes is the one conversion from a scheme to what ImGui draws with; SearchPreview
        -- draws the references panel's preview pane through it, and needs both here for the
        -- same reason it needs Search below.
        "%{wks.location}/Graphite/src/Editor/Palettes.h",
        "%{wks.location}/Graphite/src/Editor/Palettes.cpp",
        "%{wks.location}/Graphite/src/Editor/ExpectedWrites.h",
        "%{wks.location}/Graphite/src/Editor/ExpectedWrites.cpp",
        "%{wks.location}/Graphite/src/Theme/**.h",
        "%{wks.location}/Graphite/src/Theme/**.cpp",
        "%{wks.location}/Graphite/src/Search/**.h",
        "%{wks.location}/Graphite/src/Search/**.cpp",
        -- SplitResults now lives here rather than under Search, and Git needs it for the same
        -- layout the panels above already draw.
        "%{wks.location}/Graphite/src/UI/**.h",
        "%{wks.location}/Graphite/src/UI/**.cpp",
        -- ReferencesPanel carries AsHits, the pure function this project tests; it and
        -- SearchPreview - the pane it draws through - are named individually rather than
        -- globbing all of Panels, which drags in the rest of the application's windows.
        "%{wks.location}/Graphite/src/Panels/ReferencesPanel.h",
        "%{wks.location}/Graphite/src/Panels/ReferencesPanel.cpp",
        "%{wks.location}/Graphite/src/Panels/SearchPreview.h",
        "%{wks.location}/Graphite/src/Panels/SearchPreview.cpp",
        -- DroppedPaths is the rule a drag and drop is routed by, and reaches no further
        -- than the standard library; the rest of Workspace drags in the file tree, the
        -- watcher and libgit2, so it is named here rather than globbed.
        "%{wks.location}/Graphite/src/Workspace/DroppedPaths.h",
        "%{wks.location}/Graphite/src/Workspace/DroppedPaths.cpp",
        "%{wks.location}/Graphite/src/Git/**.h",
        "%{wks.location}/Graphite/src/Git/**.cpp",
    }

    includedirs
    {
        "%{wks.location}/Core/src",
        "%{wks.location}/Graphite/src"
    }

    externalincludedirs
    {
        "%{IncludeDir.doctest}",
        "%{IncludeDir.libgit2}",
        "%{IncludeDir.pugixml}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.MdiIcons}",
        "%{IncludeDir.spdlog}",
        "%{IncludeDir.json}"
    }

    externalwarnings "Off"

    links { "Core", "GLFW", "ImGui", "libgit2", "pugixml", "opengl32.lib", "dwmapi.lib", "ole32.lib", "shell32.lib", "ws2_32.lib", "secur32.lib" }

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
