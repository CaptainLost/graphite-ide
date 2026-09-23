project "ImGui"
    kind "StaticLib"
    language "C++"
    cppdialect "C++20"
    staticruntime "on"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "%{wks.location}/vendor/imgui/imconfig.h",
        "%{wks.location}/vendor/imgui/imgui.h",
        "%{wks.location}/vendor/imgui/imgui.cpp",
        "%{wks.location}/vendor/imgui/imgui_draw.cpp",
        "%{wks.location}/vendor/imgui/imgui_internal.h",
        "%{wks.location}/vendor/imgui/imgui_tables.cpp",
        "%{wks.location}/vendor/imgui/imgui_widgets.cpp",
        "%{wks.location}/vendor/imgui/imgui_demo.cpp",
        "%{wks.location}/vendor/imgui/imstb_rectpack.h",
        "%{wks.location}/vendor/imgui/imstb_textedit.h",
        "%{wks.location}/vendor/imgui/imstb_truetype.h",
        "%{wks.location}/vendor/imgui/backends/imgui_impl_glfw.h",
        "%{wks.location}/vendor/imgui/backends/imgui_impl_glfw.cpp",
        "%{wks.location}/vendor/imgui/backends/imgui_impl_opengl3.h",
        "%{wks.location}/vendor/imgui/backends/imgui_impl_opengl3.cpp",
        -- Built here rather than as a project of its own: it is two files that compile
        -- against ImGui's internals and link nowhere ImGui does not already.
        "%{wks.location}/vendor/imanim/im_anim.h",
        "%{wks.location}/vendor/imanim/im_anim.cpp"
    }

    includedirs
    {
        "%{wks.location}/vendor/imgui",
        "%{wks.location}/vendor/imgui/backends",
        "%{IncludeDir.GLFW}"
    }

    filter "system:windows"
        systemversion "latest"
        defines { "_CRT_SECURE_NO_WARNINGS" }

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
