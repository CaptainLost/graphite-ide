#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/Events/Event.h"

struct ImFont;

namespace Core {

	class Window;

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