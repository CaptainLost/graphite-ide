#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Events/Event.h"

struct ImFont;

namespace Core {

	class Window;

	// Whether the GUI is entitled to a keystroke, and so whether the event stops here.
	//
	// Deliberately not ImGui's WantCaptureKeyboard: with keyboard navigation enabled that
	// flag is true for any focused window that allows navigation, meaning "the GUI could
	// navigate" rather than "the GUI is using this key". Consuming on it swallows every
	// keystroke in the application. An active item is the honest signal — a text field being
	// typed into sets one; a table cell merely clicked does not.
	bool GuiConsumesKeyboard(bool anyItemActive, bool modalOpen);

	class ImGuiRenderer final : NonCopyable
	{
	public:
		ImGuiRenderer() = default;
		~ImGuiRenderer();

		Result<void> Init(Window& window);
		void Shutdown();

		void BeginFrame();
		void EndFrame(Window& window);

		// Consumes the input the GUI is currently claiming, so a keystroke aimed at a focused
		// text field never also reaches whatever is listening for shortcuts below. This is the
		// whole reason events are consumable: without it, typing a letter in a rename field
		// would run the command bound to that letter as well.
		void OnEvent(Event& event);

		static ImFont* GetMonoFont();

	private:
		bool m_Initialized = false;
	};
}