#pragma once

#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Events/Event.h"
#include "Core/ImGui/ImGuiErrorLog.h"

#include <filesystem>
#include <string>

struct ImFont;

namespace Core {

	struct ChromePalette;
	class ImGuiTextureFactory;
	class TextureFactory;
	class Window;

	// WARNING: ImGui's WantCaptureKeyboard is true for any focused window that allows
	// navigation, so consuming on it swallows every keystroke in the application.
	bool GuiConsumesKeyboard(bool wantsTextInput, bool popupOpen);

	class ImGuiRenderer final : NonCopyable
	{
	public:
		static Result<Scope<ImGuiRenderer>> Create(Window& window, const std::filesystem::path& layoutFile);

		~ImGuiRenderer();

		void BeginFrame();
		void EndFrame(Window& window);

		void OnEvent(Event& event);

		bool HasPendingInput() const;

		bool IsPointerHeld() const;

		void HideDetachedWindows();

		static ImFont* GetInterfaceFont();

		static ImFont* GetMonoFont();

		static ImFont* GetBoldFont();

		static ImFont* GetBadgeFont();

		static ImFont* GetItalicFont();

		static void ApplyTheme(const ChromePalette& palette);

		static void SetInterfaceScale(float scale);

		static TextureFactory& GetTextures();

	private:
		ImGuiRenderer();

		Result<void> Init(Window& window, const std::filesystem::path& layoutFile);

		void UpdateStyleScale();

		// WARNING: ImGui stores the pointer rather than copying the string, so this has to
		// outlive the context.
		std::string m_LayoutFile;
		ImGuiErrorLog m_ErrorLog;
		Scope<ImGuiTextureFactory> m_Textures;
		float m_InitialDpiScale = 1.0f;
		float m_StyleScale = 0.0f;
		bool m_ViewportDpiKnown = false;

		bool m_Initialized = false;
	};
}