#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Events/Event.h"
#include "Core/ImGui/ImGuiErrorLog.h"

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
		ImGuiRenderer();
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

		// The user's own scale for the whole interface, on top of the monitor's: 1.0 draws
		// the chrome and its text at the size the system asks for. Text follows at once; the
		// style's sizes are rebuilt at the start of the next frame, since ImGui forbids
		// changing them inside one.
		static void SetInterfaceScale(float scale);

	private:
		// ImGui stores the pointer rather than copying the string, so the storage has to
		// outlive the context.
		// Rebuilds the style's sizes when the main window's monitor scale or the user's scale
		// has changed since they were last built. Called before NewFrame, where changing the
		// style is allowed.
		void UpdateStyleScale();

		std::string m_LayoutFile;
		ImGuiErrorLog m_ErrorLog;
		// The window's content scale when it was created, standing in for the main viewport's
		// until the first frame has told ImGui which monitor it is on.
		float m_InitialDpiScale = 1.0f;
		// The scale the style's sizes were last built at; zero until the first frame.
		float m_StyleScale = 0.0f;
		bool m_Initialized = false;
	};
}