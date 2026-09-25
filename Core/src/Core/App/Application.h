#pragma once

#include "Core/App/LayerStack.h"
#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/Platform/Window.h"

#include <memory>
#include <optional>
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

		std::optional<WindowPlacement> Placement;
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

		// Entry point for everything the window reports. The GUI gets first refusal, then the
		// layers from the top down, and the walk stops at the first one that consumes.
		void OnEvent(Event& event);

	protected:
		virtual void OnInit() {}
		virtual void OnShutdown() {}

	private:
		Result<void> OpenWindowAndGui();
		void RefreshFrameLayers();
		bool EveryLayerAllowsClose();
		bool HasNoPixels() const;

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