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
        -- Sessions is the transcript model and nothing else: it draws nothing, and its windows
        -- live under Panels, so the whole folder is globbed.
        "%{wks.location}/Graphite/src/Sessions/**.h",
        "%{wks.location}/Graphite/src/Sessions/**.cpp",
        -- Persistence is plain state and its encoding: it draws nothing, so the whole folder
        -- is globbed.
        "%{wks.location}/Graphite/src/Persistence/**.h",
        "%{wks.location}/Graphite/src/Persistence/**.cpp",
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
        "%{wks.location}/Graphite/src/Editor/ScrollAnchor.h",
        "%{wks.location}/Graphite/src/Editor/ScrollAnchor.cpp",
        "%{wks.location}/Graphite/src/Editor/SemanticOverlay.h",
        "%{wks.location}/Graphite/src/Editor/SemanticOverlay.cpp",
        "%{wks.location}/Graphite/src/Editor/DiagnosticOverlay.h",
        "%{wks.location}/Graphite/src/Editor/DiagnosticOverlay.cpp",
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
        -- ConsoleSelection is what the console's mouse sweep comes to as a range and as
        -- text; it holds no ImGui, so it is tested here alongside the two panels above.
        "%{wks.location}/Graphite/src/Panels/ConsoleSelection.h",
        "%{wks.location}/Graphite/src/Panels/ConsoleSelection.cpp",
        -- DroppedPaths is the rule a drag and drop is routed by, and reaches no further
        -- than the standard library; the rest of Workspace drags in the file tree, the
        -- watcher and libgit2, so it is named here rather than globbed.
        "%{wks.location}/Graphite/src/Workspace/DroppedPaths.h",
        "%{wks.location}/Graphite/src/Workspace/DroppedPaths.cpp",
        -- OwnStateChanges is the same kind of rule, for which watcher batches are the IDE's own
        -- writing, and reaches no further than Core's path helpers.
        "%{wks.location}/Graphite/src/Workspace/OwnStateChanges.h",
        "%{wks.location}/Graphite/src/Workspace/OwnStateChanges.cpp",
        -- WorkspaceTrust is the list of trusted folders and its file; it reaches no further
        -- than Core's path and file helpers. The trust settings section, which Settings'
        -- glob brings in, links against it.
        "%{wks.location}/Graphite/src/Workspace/WorkspaceTrust.h",
        "%{wks.location}/Graphite/src/Workspace/WorkspaceTrust.cpp",
        "%{wks.location}/Graphite/src/Git/**.h",
        "%{wks.location}/Graphite/src/Git/**.cpp",
        -- The menu tree is data: it names commands and groups without reaching for any of
        -- them, so the shape of the menu can be asserted here without the application.
        "%{wks.location}/Graphite/src/App/GraphiteMenu.h",
        "%{wks.location}/Graphite/src/App/GraphiteMenu.cpp",
    }

    -- The one file under Git that is a window rather than logic: its rows open a file
    -- through DocumentWindows, and linking that call here would drag the whole text editor
    -- in behind it - the same reason Editor and Panels above are named file by file instead
    -- of globbed. Nothing under tests/src names this panel; the row logic it draws, and the
    -- discard question it composes, are in ChangesRows, which stays. The way back in for a
    -- test that really needs the panel is to publish OpenFileRequested on the event bus
    -- instead of calling DocumentWindows, which is what pulls the editor in behind it.
    removefiles { "%{wks.location}/Graphite/src/Git/ChangesPanel.cpp" }

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
        "%{IncludeDir.ImPlot}",
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
