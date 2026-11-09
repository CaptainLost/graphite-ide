-- CoreLinux: the Linux build of Core's headless part and its tests, done by zig (build.zig at the
-- repository root, ADR 0693). This project only lets MSBuild and the solution run that build;
-- it compiles nothing itself. The test binary lands in bin/<Configuration>-linux-x86_64 and is
-- run in WSL by scripts/Test-CoreLinux.ps1.
--
-- WARNING: the commands run from this project's directory, and paths in them are written
-- relative to it: %{wks.location} becomes $(SolutionDir), which is the repository root only
-- when the build goes through the solution.

project "CoreLinux"
    kind "Makefile"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files { "%{wks.location}/build.zig" }

    -- Dist has no Linux meaning of its own; it builds what Release builds.
    local zig = "..\\..\\vendor\\bin\\zig\\zig.exe"
    local missing = "if not exist " .. zig .. " (echo zig not found: run scripts/Get-Zig.ps1 & exit /b 1)"

    filter "configurations:Debug"
        buildcommands { missing, "cd ..\\.. && vendor\\bin\\zig\\zig.exe build -Dconfiguration=Debug --cache-dir bin-int\\Debug-linux-x86_64\\zig-cache" }
        rebuildcommands { missing, "cd ..\\.. && vendor\\bin\\zig\\zig.exe build -Dconfiguration=Debug --cache-dir bin-int\\Debug-linux-x86_64\\zig-cache" }
        cleancommands { "cd ..\\.. && if exist bin\\Debug-linux-x86_64 rmdir /s /q bin\\Debug-linux-x86_64" }
    filter "configurations:Release or Dist"
        buildcommands { missing, "cd ..\\.. && vendor\\bin\\zig\\zig.exe build -Dconfiguration=Release --cache-dir bin-int\\Release-linux-x86_64\\zig-cache" }
        rebuildcommands { missing, "cd ..\\.. && vendor\\bin\\zig\\zig.exe build -Dconfiguration=Release --cache-dir bin-int\\Release-linux-x86_64\\zig-cache" }
        cleancommands { "cd ..\\.. && if exist bin\\Release-linux-x86_64 rmdir /s /q bin\\Release-linux-x86_64" }
    filter {}
