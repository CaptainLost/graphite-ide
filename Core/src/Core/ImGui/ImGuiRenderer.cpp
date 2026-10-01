#include "corepch.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/Animation.h"
#include "Core/ImGui/Theme.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Platform/Process.h"
#include "Core/Platform/Window.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <string>

#ifdef CORE_PLATFORM_WINDOWS
// <GL/gl.h> on Windows depends on typedefs from <windows.h>, so windows.h comes first.
#include <windows.h>

#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>

namespace Core {

	namespace {
		constexpr float kBaseFontSize = 15.0f;

		ImFont* g_monoFont = nullptr;

		ImFont* LoadTextFont(ImGuiIO& io, const Result<std::filesystem::path>& directory)
		{
			ImFontConfig config;
			config.SizePixels = kBaseFontSize;
			// Without this a missing .ttf trips ImGui's IM_ASSERT_USER_ERROR before the call
			// returns nullptr, which crashes a Debug build instead of letting the warning run.
			config.Flags |= ImFontFlags_NoLoadError;

			if (directory)
			{
				const auto path = directory.GetValue() / "JetBrainsMono-Regular.ttf";

				if (ImFont* loaded = io.Fonts->AddFontFromFileTTF(ToUtf8(path).c_str(),
																  kBaseFontSize, &config))
				{
					return loaded;
				}
			}

			// ProggyClean has no glyphs beyond Latin-1, so Polish file names will show as
			// empty boxes — but an IDE that starts is better than one that does not.
			CORE_WARN("Could not load JetBrains Mono, falling back to the built-in font");

			ImFontConfig fallbackConfig;
			fallbackConfig.SizePixels = kBaseFontSize;

			return io.Fonts->AddFontDefault(&fallbackConfig);
		}

		// Must run immediately after the text face is added: ImGui's dynamic atlas merges onto
		// whichever font was added last, so any face added in between would take the merge
		// with it and leave the text face without icons.
		void MergeIconFont(ImGuiIO& io, const Result<std::filesystem::path>& directory)
		{
			if (!directory)
			{
				CORE_WARN("Cannot locate the executable directory, icons will not load: {}",
						  directory.GetError().Message);
				return;
			}

			ImFontConfig config;
			config.MergeMode = true;
			// Setting the minimum advance to the font size rather than enabling PixelSnapH,
			// which rounds each glyph's advance to a whole pixel and visibly skews a small
			// round icon.
			config.GlyphMinAdvanceX = kBaseFontSize;
			// The icon webfont declares vertical metrics built for CSS line-height rather than
			// for a text baseline, so merged as-is its glyphs sit above the surrounding text.
			config.GlyphOffset = ImVec2(0.0f, 1.5f);
			// Without this a missing .ttf trips ImGui's IM_ASSERT_USER_ERROR before the call
			// returns nullptr, which crashes a Debug build instead of letting the warning run.
			config.Flags |= ImFontFlags_NoLoadError;

			const auto path = directory.GetValue() / "materialdesignicons-webfont.ttf";

			if (io.Fonts->AddFontFromFileTTF(ToUtf8(path).c_str(), kBaseFontSize, &config,
											 UsedGlyphRanges()) == nullptr)
			{
				CORE_WARN("Could not load the icon font, the UI will show text only: {}",
						  ToUtf8(path));
			}
		}

		bool ViewportsEnabled()
		{
			return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
		}
	}

	ImFont* ImGuiRenderer::GetMonoFont()
	{
		return g_monoFont;
	}

	// Exists because applying a palette is not a complete operation on its own: Theme::Apply
	// paints the colours, and an undocked panel's system window needs an opaque background
	// that the palette's own may not be. Every caller outside Core reaches for this rather
	// than Theme::Apply, so a re-apply mid-run leaves the style where startup left it.
	void ImGuiRenderer::ApplyTheme(const ChromePalette& palette)
	{
		Theme::Apply(palette);

		if (ViewportsEnabled())
		{
			ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
	}

	void ImGuiRenderer::SetInterfaceScale(float scale)
	{
		ImGui::GetStyle().FontScaleMain = scale;
	}

	void ImGuiRenderer::UpdateStyleScale()
	{
		// Zero until ImGui has placed the main viewport on a monitor, at the end of the first
		// frame's NewFrame.
		const float viewportDpi = ImGui::GetMainViewport()->DpiScale;
		const float dpi = viewportDpi > 0.0f ? viewportDpi : m_InitialDpiScale;

		ImGuiStyle& style = ImGui::GetStyle();
		const float scale = style.FontScaleMain * dpi;

		if (scale == m_StyleScale)
		{
			return;
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
		Shutdown();
	}

	Result<void> ImGuiRenderer::Init(Window& window, const std::filesystem::path& layoutFile)
	{
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		m_ErrorLog.Attach(*ImGui::GetCurrentContext());

		// Beside ImGui's own and for exactly as long: every plot call reads it through a
		// global, as ImGui's calls read theirs.
		ImPlot::CreateContext();

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		m_LayoutFile = layoutFile.empty() ? std::string{} : ToUtf8(layoutFile);
		io.IniFilename = m_LayoutFile.empty() ? nullptr : m_LayoutFile.c_str();

		// Otherwise a window can be dragged by any empty part of its body, which makes
		// context menus and popups draggable — they have no title bar, so their whole
		// surface counts.
		io.ConfigWindowsMoveFromTitleBarOnly = true;

		// The text in each window follows the scale of the monitor that window is on. The
		// style's sizes cannot follow per window - ImGui has one style - so they follow the
		// main window's monitor, in UpdateStyleScale.
		io.ConfigDpiScaleFonts = true;

#ifndef CORE_DEBUG
		// A misuse of ImGui's API that it knows how to recover from - an unbalanced
		// Begin/End, a cursor moved past the last item - asserts by default, which a user
		// sees as the IDE vanishing. Outside Debug it is recovered from and written to the
		// log instead; the tooltip ImGui would otherwise draw over the window is for the
		// developer, not the user.
		io.ConfigErrorRecoveryEnableAssert = false;
		io.ConfigErrorRecoveryEnableTooltip = false;
#endif

		const auto executableDirectory = ExecutableDirectory();

		g_monoFont = LoadTextFont(io, executableDirectory);
		MergeIconFont(io, executableDirectory);

		ApplyTheme(Theme::DefaultPalette());

		auto* handle = static_cast<GLFWwindow*>(window.GetNativeHandle());

		float contentScaleY = 1.0f;
		glfwGetWindowContentScale(handle, &m_InitialDpiScale, &contentScaleY);
		ImGui::GetStyle().FontScaleDpi = m_InitialDpiScale;
		m_StyleScale = 0.0f;
		if (!ImGui_ImplGlfw_InitForOpenGL(handle, true))
		{
			ImPlot::DestroyContext();
			ImGui::DestroyContext();
			return Error{ErrorCode::Unsupported, "ImGui_ImplGlfw_InitForOpenGL failed"};
		}

		if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
		{
			ImGui_ImplGlfw_Shutdown();
			ImPlot::DestroyContext();
			ImGui::DestroyContext();
			return Error{ErrorCode::Unsupported, "ImGui_ImplOpenGL3_Init failed"};
		}

		m_Initialized = true;
		CORE_INFO("ImGui initialized (docking + viewports)");
		return Result<void>::Ok();
	}

	void ImGuiRenderer::Shutdown()
	{
		if (!m_Initialized)
			return;

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
		m_Initialized = false;
	}

	bool GuiConsumesKeyboard(bool wantsTextInput, bool popupOpen)
	{
		return wantsTextInput || popupOpen;
	}

	void ImGuiRenderer::OnEvent(Event& event)
	{
		if (!m_Initialized)
			return;

		const ImGuiIO& io = ImGui::GetIO();

		const bool popupOpen =
			ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

		if (event.IsInCategory(EventCategoryKeyboard) && GuiConsumesKeyboard(io.WantTextInput, popupOpen))
		{
			event.Handled = true;
		}

		// WantCaptureMouse is used directly: keyboard navigation does not inflate it the way
		// it does WantCaptureKeyboard, so it still means what it says.
		if (event.IsInCategory(EventCategoryMouse) && io.WantCaptureMouse)
		{
			event.Handled = true;
		}
	}

	bool ImGuiRenderer::HasPendingInput() const
	{
		return m_Initialized && !ImGui::GetCurrentContext()->InputEventsQueue.empty();
	}

	bool ImGuiRenderer::IsPointerHeld() const
	{
		return m_Initialized && ImGui::IsAnyMouseDown();
	}

	void ImGuiRenderer::BeginFrame()
	{
		UpdateStyleScale();

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