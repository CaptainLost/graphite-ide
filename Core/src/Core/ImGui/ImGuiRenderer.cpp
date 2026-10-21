#include "corepch.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/DetachedWindows.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Platform/Window.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <string>

#ifdef CORE_PLATFORM_WINDOWS
// WARNING: <GL/gl.h> on Windows depends on typedefs from <windows.h>, so windows.h comes first.
#include <windows.h>

#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>

namespace Core {

	namespace {
		constexpr float kOpaqueAlpha = 1.0f;
		constexpr float kStyleScaleNotBuilt = 0.0f;
		constexpr float kViewportDpiNotReported = 0.0f;
		constexpr bool kInstallPlatformCallbacks = true;
		constexpr const char* kAnyPopupName = nullptr;
		constexpr int kViewportOrigin = 0;
		constexpr float kClearRed = 0.106f;
		constexpr float kClearGreen = 0.114f;
		constexpr float kClearBlue = 0.129f;
		constexpr float kClearAlpha = 1.0f;

		FontSet g_fonts;

		void DestroyContexts()
		{
			ImPlot::DestroyContext();
			ImGui::DestroyContext();
			g_fonts = {};
		}

		bool ViewportsEnabled()
		{
			return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
		}
	}

	ImFont* ImGuiRenderer::GetInterfaceFont()
	{
		return g_fonts.Interface;
	}

	ImFont* ImGuiRenderer::GetMonoFont()
	{
		return g_fonts.Code;
	}

	ImFont* ImGuiRenderer::GetBoldFont()
	{
		return g_fonts.Strong;
	}

	ImFont* ImGuiRenderer::GetBadgeFont()
	{
		return g_fonts.Badge;
	}

	ImFont* ImGuiRenderer::GetItalicFont()
	{
		return g_fonts.Italic;
	}

	void ImGuiRenderer::ApplyTheme(const ChromePalette& palette)
	{
		Theme::Apply(palette);

		if (ViewportsEnabled())
		{
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = kOpaqueAlpha;
		}
	}

	void ImGuiRenderer::SetInterfaceScale(float scale)
	{
		ImGui::GetStyle().FontScaleMain = scale;
	}

	void ImGuiRenderer::UpdateStyleScale()
	{
		const float viewportDpi = ImGui::GetMainViewport()->DpiScale;
		const bool viewportDpiReported = viewportDpi > kViewportDpiNotReported;
		const float dpi = viewportDpiReported ? viewportDpi : m_InitialDpiScale;

		ImGuiStyle& style = ImGui::GetStyle();
		const float scale = style.FontScaleMain * dpi;

		const bool sourceSwitched = viewportDpiReported && !m_ViewportDpiKnown;
		m_ViewportDpiKnown = viewportDpiReported;

		// WARNING: TakeMetricsChanged has to run every frame, or a change made at an unchanged scale would be seen only at the next scale.
		const bool metricsChanged = Theme::TakeMetricsChanged();

		if (scale == m_StyleScale && !metricsChanged)
		{
			return;
		}

		// WARNING: this runs between frames and not where the scale is set, because dock nodes
		// are laid out during a frame.
		if (!sourceSwitched)
		{
			ScaleDockNodeSizes(ScaleChangeFactor(m_StyleScale, scale));
		}

		style = ScaledStyle(style, scale, ViewportsEnabled());
		m_StyleScale = scale;
	}

	ImGuiRenderer::ImGuiRenderer()
		: m_ErrorLog(*Log::GetEngineLogger())
	{
	}

	ImGuiRenderer::~ImGuiRenderer()
	{
		if (!m_Initialized)
		{
			return;
		}

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		DestroyContexts();
	}

	Result<Scope<ImGuiRenderer>> ImGuiRenderer::Create(Window& window, const std::filesystem::path& layoutFile)
	{
		// WARNING: not make_unique, because the constructor is private.
		Scope<ImGuiRenderer> renderer{new ImGuiRenderer};

		if (Result<void> initialised = renderer->Init(window, layoutFile); !initialised)
		{
			return initialised.GetError();
		}

		return renderer;
	}

	Result<void> ImGuiRenderer::Init(Window& window, const std::filesystem::path& layoutFile)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		m_ErrorLog.Attach(*ImGui::GetCurrentContext());

		ImPlot::CreateContext();

		ImGuiIO& inputOutput = ImGui::GetIO();
		inputOutput.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		inputOutput.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		inputOutput.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		m_LayoutFile = layoutFile.empty() ? std::string{} : ToUtf8(layoutFile);
		inputOutput.IniFilename = m_LayoutFile.empty() ? nullptr : m_LayoutFile.c_str();

		inputOutput.ConfigWindowsMoveFromTitleBarOnly = true;

		inputOutput.ConfigDpiScaleFonts = true;

#ifndef CORE_DEBUG
		inputOutput.ConfigErrorRecoveryEnableAssert = false;
		inputOutput.ConfigErrorRecoveryEnableTooltip = false;
#endif

		g_fonts = LoadFontSet(*inputOutput.Fonts);
		inputOutput.FontDefault = g_fonts.Interface;

		ApplyTheme(Theme::DefaultPalette());

		GLFWwindow* handle = static_cast<GLFWwindow*>(window.GetNativeHandle());

		float contentScaleY = 1.0f;
		glfwGetWindowContentScale(handle, &m_InitialDpiScale, &contentScaleY);
		ImGui::GetStyle().FontScaleDpi = m_InitialDpiScale;
		m_StyleScale = kStyleScaleNotBuilt;

		if (!ImGui_ImplGlfw_InitForOpenGL(handle, kInstallPlatformCallbacks))
		{
			DestroyContexts();
			return Error{ErrorCode::Unsupported, "ImGui_ImplGlfw_InitForOpenGL failed"};
		}

		if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
		{
			ImGui_ImplGlfw_Shutdown();
			DestroyContexts();
			return Error{ErrorCode::Unsupported, "ImGui_ImplOpenGL3_Init failed"};
		}

		InstallDetachedWindowHooks(ImGui::GetPlatformIO());

		m_Initialized = true;
		CORE_INFO("ImGui initialized (docking + viewports)");
		return Result<void>::Ok();
	}

	bool GuiConsumesKeyboard(bool wantsTextInput, bool popupOpen)
	{
		return wantsTextInput || popupOpen;
	}

	void ImGuiRenderer::OnEvent(Event& event)
	{
		const ImGuiIO& inputOutput = ImGui::GetIO();

		const bool popupOpen = ImGui::IsPopupOpen(
			kAnyPopupName, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

		if (event.IsInCategory(EventCategory::Keyboard) &&
			GuiConsumesKeyboard(inputOutput.WantTextInput, popupOpen))
		{
			event.Handled = true;
		}

		if (event.IsInCategory(EventCategory::Mouse) && inputOutput.WantCaptureMouse)
		{
			event.Handled = true;
		}
	}

	bool ImGuiRenderer::HasPendingInput() const
	{
		return !ImGui::GetCurrentContext()->InputEventsQueue.empty();
	}

	bool ImGuiRenderer::IsPointerHeld() const
	{
		return ImGui::IsAnyMouseDown();
	}

	void ImGuiRenderer::HideDetachedWindows()
	{
		const ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();

		for (const ImGuiViewport* viewport : platformIo.Viewports)
		{
			const bool isMainWindow = viewport->ID == ImGui::GetMainViewport()->ID;

			if (isMainWindow || viewport->PlatformHandle == nullptr)
			{
				continue;
			}

			glfwHideWindow(static_cast<GLFWwindow*>(viewport->PlatformHandle));
		}
	}

	void ImGuiRenderer::BeginFrame()
	{
		UpdateStyleScale();

		ApplyPendingDetachedWindowActions();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		Animation::BeginFrame();
	}

	void ImGuiRenderer::EndFrame(Window& window)
	{
		ImGui::Render();

		int width = 0;
		int height = 0;
		window.FramebufferSize(width, height);

		glViewport(kViewportOrigin, kViewportOrigin, width, height);
		glClearColor(kClearRed, kClearGreen, kClearBlue, kClearAlpha);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		const ImGuiIO& inputOutput = ImGui::GetIO();
		if ((inputOutput.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
		{
			GLFWwindow* previousContext = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();

			if (AskDetachedWindowsForTheirSystemRects(ImGui::GetPlatformIO()))
			{
				Redraw::Request();
			}

			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(previousContext);
		}
	}
}