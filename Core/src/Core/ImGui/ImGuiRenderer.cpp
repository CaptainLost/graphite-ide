#include "corepch.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include "Core/Base/Log.h"
#include "Core/FileSystem/FileSystem.h"
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
// WARNING: <GL/gl.h> on Windows depends on typedefs from <windows.h>, so windows.h comes first.
#include <windows.h>

#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <GLFW/glfw3.h>

namespace Core {

	namespace {
		constexpr float kBaseFontSize = 15.0f;
		constexpr float kIconBaselineLift = 1.5f;
		constexpr char kAdvanceSampleCharacter = '0';
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

		ImFont* g_monoFont = nullptr;
		ImFont* g_boldFont = nullptr;

		void DestroyContexts()
		{
			ImPlot::DestroyContext();
			ImGui::DestroyContext();
			g_monoFont = nullptr;
			g_boldFont = nullptr;
		}

		// WARNING: without NoLoadError a missing .ttf trips ImGui's IM_ASSERT_USER_ERROR before
		// the call returns nullptr, which crashes a Debug build instead of letting the warning run.
		void AllowMissingFontFile(ImFontConfig& config)
		{
			config.Flags |= ImFontFlags_NoLoadError;
		}

		ImFont* LoadTextFont(ImGuiIO& inputOutput, const Result<std::filesystem::path>& directory)
		{
			ImFontConfig config;
			config.SizePixels = kBaseFontSize;
			AllowMissingFontFile(config);

			if (directory)
			{
				const std::filesystem::path path = directory.GetValue() / "JetBrainsMono-Regular.ttf";

				if (ImFont* loaded = inputOutput.Fonts->AddFontFromFileTTF(
						ToUtf8(path).c_str(), kBaseFontSize, &config))
				{
					return loaded;
				}
			}

			// WARNING: ProggyClean has no glyphs beyond Latin-1, so Polish file names show as
			// empty boxes.
			CORE_WARN("Could not load JetBrains Mono, falling back to the built-in font");

			ImFontConfig fallbackConfig;
			fallbackConfig.SizePixels = kBaseFontSize;

			return inputOutput.Fonts->AddFontDefault(&fallbackConfig);
		}

		// WARNING: must run immediately after the text face is added, because ImGui's dynamic
		// atlas merges onto whichever font was added last.
		void MergeIconFont(ImGuiIO& inputOutput, const Result<std::filesystem::path>& directory)
		{
			if (!directory)
			{
				CORE_WARN("Cannot locate the executable directory, icons will not load: {}",
						  directory.GetError().Message);
				return;
			}

			ImFontConfig config;
			config.MergeMode = true;
			// WARNING: PixelSnapH rounds each glyph's advance to a whole pixel and visibly
			// skews a small round icon, so the minimum advance is set instead.
			config.GlyphMinAdvanceX = kBaseFontSize;
			config.GlyphOffset = ImVec2(0.0f, kIconBaselineLift);
			AllowMissingFontFile(config);

			const std::filesystem::path path = directory.GetValue() / "materialdesignicons-webfont.ttf";

			if (inputOutput.Fonts->AddFontFromFileTTF(ToUtf8(path).c_str(), kBaseFontSize, &config,
													  UsedGlyphRanges()) == nullptr)
			{
				CORE_WARN("Could not load the icon font, the UI will show text only: {}",
						  ToUtf8(path));
			}
		}

		// WARNING: the terminal draws a run of cells with one AddText call, so a proportional
		// symbol wider than a cell would push the rest of its row off its cells. The advance is
		// pinned to the text face's own to keep every fallback glyph one cell wide.
		void MergeSymbolFallback(ImGuiIO& inputOutput, ImFont* textFont)
		{
			const Result<std::filesystem::path> fonts = SystemFontsDirectory();

			if (!fonts)
			{
				CORE_WARN("Cannot locate the system fonts, symbols will show as boxes: {}",
						  fonts.GetError().Message);
				return;
			}

			const float cellAdvance = textFont->GetFontBaked(kBaseFontSize)->GetCharAdvance(kAdvanceSampleCharacter);

			ImFontConfig config;
			config.MergeMode = true;
			config.GlyphMinAdvanceX = cellAdvance;
			config.GlyphMaxAdvanceX = cellAdvance;
			AllowMissingFontFile(config);

			const std::filesystem::path path = fonts.GetValue() / "seguisym.ttf";

			if (inputOutput.Fonts->AddFontFromFileTTF(ToUtf8(path).c_str(), kBaseFontSize,
													  &config) == nullptr)
			{
				CORE_WARN("Could not load the symbol font, symbols will show as boxes: {}",
						  ToUtf8(path));
			}
		}

		// WARNING: must run after every merge onto the text face, because ImGui's dynamic atlas
		// merges onto whichever font was added last.
		ImFont* LoadBoldFont(ImGuiIO& inputOutput, const Result<std::filesystem::path>& directory)
		{
			if (!directory)
			{
				CORE_WARN("Cannot locate the executable directory, the bold face will not load: {}",
						  directory.GetError().Message);
				return nullptr;
			}

			ImFontConfig config;
			config.SizePixels = kBaseFontSize;
			AllowMissingFontFile(config);

			const std::filesystem::path path = directory.GetValue() / "JetBrainsMono-Bold.ttf";
			ImFont* loaded =
				inputOutput.Fonts->AddFontFromFileTTF(ToUtf8(path).c_str(), kBaseFontSize, &config);

			if (loaded == nullptr)
			{
				CORE_WARN("Could not load JetBrains Mono Bold, titles will use the regular face: {}",
						  ToUtf8(path));
			}

			return loaded;
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

	ImFont* ImGuiRenderer::GetBoldFont()
	{
		return g_boldFont;
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

		if (scale == m_StyleScale)
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

		const Result<std::filesystem::path> executableDirectory = ExecutableDirectory();

		g_monoFont = LoadTextFont(inputOutput, executableDirectory);
		MergeIconFont(inputOutput, executableDirectory);
		MergeSymbolFallback(inputOutput, g_monoFont);
		g_boldFont = LoadBoldFont(inputOutput, executableDirectory);

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

		glViewport(kViewportOrigin, kViewportOrigin, width, height);
		glClearColor(kClearRed, kClearGreen, kClearBlue, kClearAlpha);
		glClear(GL_COLOR_BUFFER_BIT);

		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		const ImGuiIO& inputOutput = ImGui::GetIO();
		if ((inputOutput.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
		{
			GLFWwindow* previousContext = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(previousContext);
		}
	}
}