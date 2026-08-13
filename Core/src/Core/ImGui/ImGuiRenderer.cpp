#include "corepch.h"

#include "Core/ImGui/ImGuiRenderer.h"

#include "Core/Base/Log.h"
#include "Core/Fs/FileSystem.h"
#include "Core/ImGui/Icons.h"
#include "Core/ImGui/Theme.h"
#include "Core/Platform/Process.h"
#include "Core/Platform/Window.h"

#include <imgui.h>
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

		// ImGui stores the pointer rather than copying the string, so the storage has to
		// outlive the context.
		const char* PersistentIniPath()
		{
			static const std::string path = [] {
				const auto directory = ExecutableDirectory();

				if (directory)
					return ToUtf8(directory.GetValue() / "imgui.ini");

				// Falling back to the working directory keeps the layout working rather than
				// losing it, but a layout that silently moves between launches is otherwise
				// very hard to explain.
				CORE_WARN("Cannot locate the executable directory, using 'imgui.ini' as-is: {}",
						  directory.GetError().Message);

				return std::string{"imgui.ini"};
			}();

			return path.c_str();
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

		// An undocked panel gets its own system window, where rounding and transparency would
		// look out of place.
		void SquareOffViewportWindows()
		{
			ImGuiStyle& style = ImGui::GetStyle();
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
	}

	ImFont* ImGuiRenderer::GetMonoFont()
	{
		return g_monoFont;
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
		io.IniFilename = PersistentIniPath();

		// Otherwise a window can be dragged by any empty part of its body, which makes
		// context menus and popups draggable — they have no title bar, so their whole
		// surface counts.
		io.ConfigWindowsMoveFromTitleBarOnly = true;

		const auto executableDirectory = ExecutableDirectory();

		g_monoFont = LoadTextFont(io, executableDirectory);
		MergeIconFont(io, executableDirectory);

		Theme::ApplyDark();

		if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0)
		{
			SquareOffViewportWindows();
		}

		auto* handle = static_cast<GLFWwindow*>(window.GetNativeHandle());
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