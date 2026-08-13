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
	// keystroke in the application.
	//
	// Nor is it "some item is active": a mouse button held down over a widget makes an item
	// active for as long as it is held, so dragging a selection in an editor would silently
	// stop the keyboard from working. Wanting text input is the narrow thing actually meant
	// — a field being typed into — and it is the case worth protecting, because naming a
	// file must not also run the commands bound to the letters of the name.
	bool GuiConsumesKeyboard(bool wantsTextInput, bool popupOpen);

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