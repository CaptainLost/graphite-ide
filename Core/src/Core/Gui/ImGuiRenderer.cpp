#include "corepch.h"

#include "Core/Gui/ImGuiRenderer.h"

#include "Core/Base/Log.h"
#include "Core/Gui/Theme.h"
#include "Core/Platform/Window.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <windows.h>

#include <GL/gl.h>

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
        io.IniFilename = "imgui.ini";

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
