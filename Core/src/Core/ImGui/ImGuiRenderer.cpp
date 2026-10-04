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
		ImFont* g_boldFont = nullptr;

		// Every way out of ImGui ends here, so the font pointer - which points into the context's
		// atlas - never outlives the context it came from.
		void DestroyContexts()
		{
			ImPlot::DestroyContext();
			ImGui::DestroyContext();
			g_monoFont = nullptr;
			g_boldFont = nullptr;
		}

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

		// Box drawing and Latin are JetBrains Mono's, but the symbols a terminal program draws
		// its interface with are not: Claude Code alone uses ⏵ ⏺ ⎿ ✻ ✢ ※ ⧉ and more, which
		// would otherwise show as empty boxes. Segoe UI Symbol has every one of them and ships
		// with Windows. Merged last, so it is consulted only for what the faces before it lack.
		//
		// Its glyphs are proportional, and the terminal draws a run of cells with one AddText
		// call that advances by each glyph's own width, so a wider symbol would push the rest of
		// its row off the cells it belongs to. Pinning the advance to the text face's own makes
		// every fallback glyph exactly one cell wide, centred in it.
		void MergeSymbolFallback(ImGuiIO& io, ImFont* textFont)
		{
			const Result<std::filesystem::path> fonts = SystemFontsDirectory();

			if (!fonts)
			{
				CORE_WARN("Cannot locate the system fonts, symbols will show as boxes: {}",
						  fonts.GetError().Message);
				return;
			}

			const float cellAdvance = textFont->GetFontBaked(kBaseFontSize)->GetCharAdvance('0');

			ImFontConfig config;
			config.MergeMode = true;
			config.GlyphMinAdvanceX = cellAdvance;
			config.GlyphMaxAdvanceX = cellAdvance;
			// Without this a missing .ttf trips ImGui's IM_ASSERT_USER_ERROR before the call
			// returns nullptr, which crashes a Debug build instead of letting the warning run.
			config.Flags |= ImFontFlags_NoLoadError;

			const auto path = fonts.GetValue() / "seguisym.ttf";

			if (io.Fonts->AddFontFromFileTTF(ToUtf8(path).c_str(), kBaseFontSize, &config) ==
				nullptr)
			{
				CORE_WARN("Could not load the symbol font, symbols will show as boxes: {}",
						  ToUtf8(path));
			}
		}

		// Must run after every merge onto the text face: ImGui's dynamic atlas merges onto
		// whichever font was added last, so adding this one earlier would hand it the icons or
		// the symbol fallback and leave the text face without them. Not merged with anything
		// itself - a title has no icons in it.
		ImFont* LoadBoldFont(ImGuiIO& io, const Result<std::filesystem::path>& directory)
		{
			if (!directory)
			{
				CORE_WARN("Cannot locate the executable directory, the bold face will not load: {}",
						  directory.GetError().Message);
				return nullptr;
			}

			ImFontConfig config;
			config.SizePixels = kBaseFontSize;
			// Without this a missing .ttf trips ImGui's IM_ASSERT_USER_ERROR before the call
			// returns nullptr, which crashes a Debug build instead of letting the warning run.
			config.Flags |= ImFontFlags_NoLoadError;

			const auto path = directory.GetValue() / "JetBrainsMono-Bold.ttf";
			ImFont* loaded = io.Fonts->AddFontFromFileTTF(ToUtf8(path).c_str(), kBaseFontSize, &config);

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

		// The first frame is built from the window's own content scale, the next ones from the
		// viewport's. Whatever differs between the two is a correction of the source, not a change
		// the user made, and the layout restored at startup is already at the scale it was saved at.
		const bool sourceSwitched = viewportDpi > 0.0f && !m_ViewportDpiKnown;
		m_ViewportDpiKnown = viewportDpi > 0.0f;

		if (scale == m_StyleScale)
		{
			return;
		}

		// Here, between frames, and not where the scale is set: dock nodes are laid out during a
		// frame. The reference sizes are pixels, and a layout restored at startup is already at the
		// scale it was saved at - the first change is from zero, which scales nothing.
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
		// Not make_unique: the constructor is private, and this is the only creator.
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
		MergeSymbolFallback(io, g_monoFont);
		g_boldFont = LoadBoldFont(io, executableDirectory);

		ApplyTheme(Theme::DefaultPalette());

		auto* handle = static_cast<GLFWwindow*>(window.GetNativeHandle());

		float contentScaleY = 1.0f;
		glfwGetWindowContentScale(handle, &m_InitialDpiScale, &contentScaleY);
		ImGui::GetStyle().FontScaleDpi = m_InitialDpiScale;
		m_StyleScale = 0.0f;
		if (!ImGui_ImplGlfw_InitForOpenGL(handle, true))
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
		const ImGuiIO& io = ImGui::GetIO();

		const bool popupOpen =
			ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

		if (event.IsInCategory(EventCategory::Keyboard) && GuiConsumesKeyboard(io.WantTextInput, popupOpen))
		{
			event.Handled = true;
		}

		// WantCaptureMouse is used directly: keyboard navigation does not inflate it the way
		// it does WantCaptureKeyboard, so it still means what it says.
		if (event.IsInCategory(EventCategory::Mouse) && io.WantCaptureMouse)
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