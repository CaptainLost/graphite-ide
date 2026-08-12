#pragma once

#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"

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

		static ImFont* GetMonoFont();

	private:
		bool m_Initialized = false;
	};
}