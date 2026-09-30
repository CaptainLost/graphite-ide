#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Events/Event.h"

#include <filesystem>
#include <string>

struct ImFont;

namespace Core {

	struct ChromePalette;
	class Window;

	// Deliberately not ImGui's WantCaptureKeyboard: with keyboard navigation enabled that
	// flag is true for any focused window that allows navigation, so consuming on it swallows
	// every keystroke in the application. Nor is it "some item is active", which stays true
	// for as long as a mouse button is held over a widget.
	bool GuiConsumesKeyboard(bool wantsTextInput, bool popupOpen);

	class ImGuiRenderer final : NonCopyable
	{
	public:
		ImGuiRenderer() = default;
		~ImGuiRenderer();

		Result<void> Init(Window& window, const std::filesystem::path& layoutFile);
		void Shutdown();

		void BeginFrame();
		void EndFrame(Window& window);

		// Consumes the input the GUI is currently claiming, so a keystroke aimed at a focused
		// text field never also reaches whatever is listening for shortcuts below.
		void OnEvent(Event& event);

		// Input the platform backend has queued and no frame has consumed yet. It covers every
		// viewport, where OnEvent sees only the main window's events; and ImGui trickles fast
		// input over several frames, so a queue left non-empty needs another one.
		bool HasPendingInput() const;

		// Whether any mouse button was down as of the last frame.
		bool IsPointerHeld() const;

		static ImFont* GetMonoFont();

		// The way to put a palette on screen. Theme::Apply paints the colours; this also runs
		// the style fixups that have to follow it, so applying a palette here means the same
		// thing at startup and on every later change of scheme.
		static void ApplyTheme(const ChromePalette& palette);

	private:
		// ImGui stores the pointer rather than copying the string, so the storage has to
		// outlive the context.
		std::string m_LayoutFile;
		bool m_Initialized = false;
	};
}