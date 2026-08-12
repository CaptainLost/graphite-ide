#pragma once

#include "Core/App/LayerStack.h"
#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/Platform/Window.h"

#include <memory>
#include <string>
#include <vector>

namespace Core {

	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;

		const char* operator[](int index) const
		{
			CORE_ASSERT(index < Count, "Command line argument index out of range");
			return Args[index];
		}
	};

	struct ApplicationSpec
	{
		std::string Name = "IDE";
		int Width = 1600;
		int Height = 900;

		bool CustomTitleBar = false;
	};

	class Application : NonCopyable
	{
	public:
		explicit Application(ApplicationSpec spec);
		virtual ~Application();

		Result<void> Run();
		void Close();

		Layer& PushLayer(Scope<Layer> layer);

		Window& GetWindow() { return *m_Window; }
		const ApplicationSpec& GetSpec() const { return m_Spec; }

		static Application& Get();

	protected:
		virtual void OnInit() {}
		virtual void OnShutdown() {}

	private:
		ApplicationSpec m_Spec;
		Scope<Window> m_Window;
		ImGuiRenderer m_Gui;
		LayerStack m_Layers;
		std::vector<Layer*> m_FrameLayers;
		bool m_Running = false;
		bool m_Minimized = false;
		double m_LastFrameTime = 0.0;

		static Application* s_Instance;
	};

	Scope<Application> CreateApplication(ApplicationCommandLineArgs args);
}