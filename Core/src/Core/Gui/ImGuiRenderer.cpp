#include "corepch.h"

#include "Core/Gui/ImGuiRenderer.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/Gui/Icons.h"
#include "Core/Gui/Theme.h"
#include "Core/Platform/Process.h"
#include "Core/Platform/Window.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <string>

#ifdef CORE_PLATFORM_WINDOWS
    // <GL/gl.h> on Windows depends on typedefs from <windows.h>, so the order matters
    // and windows.h has to come first.
    #include <windows.h>

    #include <GL/gl.h>
#else
    #include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>

namespace core
{
    ImGuiRenderer::~ImGuiRenderer()
    {
        Shutdown();
    }

    Result<void> ImGuiRenderer::Init(Window& window)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        // ImGui stores this pointer rather than copying the string, so the storage has
        // to outlive the context — a local would dangle at the next save.
        static std::string iniPath = []
        {
            const auto directory = platform::ExecutableDirectory();

            if (directory)
                return fs::ToUtf8(directory.Value() / "imgui.ini");

            // Falling back to the working directory keeps the layout working rather than
            // losing it, but say so: a layout that silently moves between launches is
            // otherwise very hard to explain.
            CORE_WARN("Cannot locate the executable directory, using 'imgui.ini' as-is: {}",
                      directory.GetError().message);

            return std::string{"imgui.ini"};
        }();

        io.IniFilename = iniPath.c_str();

        // By default ImGui lets a window be dragged by any empty part of its body, which
        // makes context menus and popups draggable — they have no title bar, so their
        // whole surface counts. Panels are moved by their dock tabs and dialogs by their
        // title bars, so nothing in this application needs the permissive behaviour.
        io.ConfigWindowsMoveFromTitleBarOnly = true;

        // The vendored ImGui (new atlas system) tags a font added with no explicit size
        // as using an "implicit reference size" and refuses to MergeMode an explicitly
        // sized font onto it (assertion in ImFontAtlasFontAdd). Giving the default font
        // an explicit SizePixels here avoids that: it matches the icon font's size below
        // and keeps both fonts at the same 13px reference size.
        ImFontConfig defaultFontConfig;
        defaultFontConfig.SizePixels = 13.0f;
        io.Fonts->AddFontDefault(&defaultFontConfig);

        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        iconConfig.PixelSnapH = true;
        iconConfig.GlyphMinAdvanceX = 13.0f;

        const auto executableDirectory = platform::ExecutableDirectory();
        if (executableDirectory)
        {
            const auto fontPath =
                executableDirectory.Value() / "materialdesignicons-webfont.ttf";

            if (io.Fonts->AddFontFromFileTTF(fs::ToUtf8(fontPath).c_str(), 13.0f, &iconConfig,
                                              UsedGlyphRanges()) == nullptr)
            {
                CORE_WARN("Could not load the icon font, the UI will show text only: {}",
                          fs::ToUtf8(fontPath));
            }
        }
        else
        {
            CORE_WARN("Cannot locate the executable directory, icons will not load: {}",
                      executableDirectory.GetError().message);
        }

        Theme::ApplyDark();

        // With undocked panels, viewports get their own system windows —
        // rounding and transparency would look out of place on them.
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        auto* handle = static_cast<GLFWwindow*>(window.NativeHandle());
        if (!ImGui_ImplGlfw_InitForOpenGL(handle, true))
        {
            ImGui::DestroyContext();
            return Error{ErrorCode::Unsupported, "ImGui_ImplGlfw_InitForOpenGL failed"};
        }

        if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
        {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            return Error{ErrorCode::Unsupported, "ImGui_ImplOpenGL3_Init failed"};
        }

        m_initialized = true;
        CORE_INFO("ImGui initialized (docking + viewports)");
        return Result<void>::Ok();
    }

    void ImGuiRenderer::Shutdown()
    {
        if (!m_initialized)
            return;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_initialized = false;
    }

    void ImGuiRenderer::BeginFrame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiRenderer::EndFrame(Window& window)
    {
        ImGui::Render();

        int width = 0;
        int height = 0;
        window.FramebufferSize(width, height);

        glViewport(0, 0, width, height);
        glClearColor(0.106f, 0.114f, 0.129f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        const ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
        {
            GLFWwindow* context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(context);
        }
    }
}
