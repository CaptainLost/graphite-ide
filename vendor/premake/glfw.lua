project "GLFW"
    kind "StaticLib"
    language "C"
    cdialect "C11"
    staticruntime "on"

    targetdir ("%{wks.location}/bin/" .. outputdir .. "/%{prj.name}")
    objdir ("%{wks.location}/bin-int/" .. outputdir .. "/%{prj.name}")

    files
    {
        "%{wks.location}/vendor/glfw/include/GLFW/glfw3.h",
        "%{wks.location}/vendor/glfw/include/GLFW/glfw3native.h",
        "%{wks.location}/vendor/glfw/src/internal.h",
        "%{wks.location}/vendor/glfw/src/platform.h",
        "%{wks.location}/vendor/glfw/src/mappings.h",
        "%{wks.location}/vendor/glfw/src/context.c",
        "%{wks.location}/vendor/glfw/src/init.c",
        "%{wks.location}/vendor/glfw/src/input.c",
        "%{wks.location}/vendor/glfw/src/monitor.c",
        "%{wks.location}/vendor/glfw/src/platform.c",
        "%{wks.location}/vendor/glfw/src/vulkan.c",
        "%{wks.location}/vendor/glfw/src/window.c",
        "%{wks.location}/vendor/glfw/src/egl_context.c",
        "%{wks.location}/vendor/glfw/src/osmesa_context.c",
        "%{wks.location}/vendor/glfw/src/null_init.c",
        "%{wks.location}/vendor/glfw/src/null_monitor.c",
        "%{wks.location}/vendor/glfw/src/null_window.c",
        "%{wks.location}/vendor/glfw/src/null_joystick.c"
    }

    filter "system:windows"
        systemversion "latest"
        files
        {
            "%{wks.location}/vendor/glfw/src/win32_init.c",
            "%{wks.location}/vendor/glfw/src/win32_module.c",
            "%{wks.location}/vendor/glfw/src/win32_joystick.c",
            "%{wks.location}/vendor/glfw/src/win32_monitor.c",
            "%{wks.location}/vendor/glfw/src/win32_time.c",
            "%{wks.location}/vendor/glfw/src/win32_thread.c",
            "%{wks.location}/vendor/glfw/src/win32_window.c",
            "%{wks.location}/vendor/glfw/src/wgl_context.c"
        }
        defines { "_GLFW_WIN32", "_CRT_SECURE_NO_WARNINGS" }

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
