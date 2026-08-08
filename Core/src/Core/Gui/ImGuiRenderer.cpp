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
    namespace
    {
        ImFont* g_monoFont = nullptr;
        ImFont* g_monoItalicFont = nullptr;
    }

    ImFont* ImGuiRenderer::MonoFont() noexcept
    {
        return g_monoFont;
    }

    ImFont* ImGuiRenderer::MonoItalicFont() noexcept
    {
        return g_monoItalicFont;
    }

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

        constexpr float kBaseFontSize = 15.0f;

        ImFontConfig textConfig;
        textConfig.SizePixels = kBaseFontSize;
        // Without this a missing .ttf trips ImGui's IM_ASSERT_USER_ERROR before the call
        // returns nullptr, which crashes a Debug build instead of letting the warning run.
        textConfig.Flags |= ImFontFlags_NoLoadError;

        const auto executableDirectory = platform::ExecutableDirectory();

        if (executableDirectory)
        {
            const auto regular = executableDirectory.Value() / "JetBrainsMono-Regular.ttf";

            g_monoFont = io.Fonts->AddFontFromFileTTF(fs::ToUtf8(regular).c_str(), kBaseFontSize,
                                                       &textConfig);
        }

        if (g_monoFont == nullptr)
        {
            // ProggyClean has no glyphs beyond Latin-1, so Polish file names will show as
            // empty boxes — but an IDE that starts is better than one that does not.
            CORE_WARN("Could not load JetBrains Mono, falling back to the built-in font");

            ImFontConfig fallbackConfig;
            fallbackConfig.SizePixels = kBaseFontSize;
            g_monoFont = io.Fonts->AddFontDefault(&fallbackConfig);
        }

        ImFontConfig iconConfig;
        iconConfig.MergeMode = true;
        // PixelSnapH rounds each glyph's horizontal advance to a whole pixel, which is
        // meant for monospaced text but visibly skews a small round icon like the refresh
        // arrows — left off so MDI's curves render at their natural proportions.
        iconConfig.GlyphMinAdvanceX = kBaseFontSize;
        // MDI's webfont declares its own vertical metrics (built for CSS line-height, not
        // for sitting on a text baseline), so merged as-is its glyphs sit visibly above the
        // surrounding text's baseline. Nudge them down to align — see
        // ImFontConfig::MergeMode's own doc comment, which calls this out by name.
        iconConfig.GlyphOffset = ImVec2(0.0f, 1.5f);
        // Without this, a missing/unreadable .ttf trips ImGui's internal
        // IM_ASSERT_USER_ERROR before AddFontFromFileTTF returns nullptr, which asserts
        // (and crashes) in Debug builds instead of letting the CORE_WARN below run.
        iconConfig.Flags |= ImFontFlags_NoLoadError;

        // Merged onto the mono face immediately after it is added — ImGui's dynamic atlas
        // merges onto whichever font was added last, so this has to happen before the
        // italic face below, or the icons would land in the italic font and never be
        // found by code that looks for them on the regular text face.
        if (executableDirectory)
        {
            const auto fontPath =
                executableDirectory.Value() / "materialdesignicons-webfont.ttf";

            if (io.Fonts->AddFontFromFileTTF(fs::ToUtf8(fontPath).c_str(), kBaseFontSize,
                                              &iconConfig, UsedGlyphRanges()) == nullptr)
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

        // Loaded last, after the icon merge above, so the icons stay attached to the
        // regular mono face rather than this one. Nothing draws with this face yet — a
        // later task uses it for preview-tab titles — so a load failure here only means
        // MonoItalicFont() returns null, which callers already have to handle.
        if (executableDirectory)
        {
            const auto italic = executableDirectory.Value() / "JetBrainsMono-Italic.ttf";

            g_monoItalicFont = io.Fonts->AddFontFromFileTTF(fs::ToUtf8(italic).c_str(),
                                                             kBaseFontSize, &textConfig);
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
