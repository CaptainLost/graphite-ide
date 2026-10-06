#pragma once

#include "Core/App/FramePacer.h"
#include "Core/App/LayerStack.h"
#include "Core/Base/Assert.h"
#include "Core/Base/Base.h"
#include "Core/Base/NonCopyable.h"
#include "Core/Base/Result.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/Platform/Window.h"

#include <filesystem>
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

		// Empty keeps no layout between runs: the docking layout then lives only as long as the process.
		std::filesystem::path LayoutFile;
	};

	class Application : NonCopyable
	{
	public:
		explicit Application(ApplicationSpec spec);
		virtual ~Application();

		Result<void> Run();
		void Close();

		Layer& PushLayer(Scope<Layer> layer);

		Window& GetWindow()
		{
			CORE_ASSERT(m_Window != nullptr, "Application::GetWindow() called outside Run");
			return *m_Window;
		}
		const ApplicationSpec& GetSpec() const { return m_Spec; }

		static Application& Get();

		void OnEvent(Event& event);

	protected:
		virtual void OnInit() {}
		virtual void OnShutdown() {}

	private:
		Result<void> OpenWindowAndGui();
		void WaitForReasonToDraw();
		void SnapshotLayersForThisFrame();
		bool EveryLayerAllowsClose();
		bool HasNoPixels() const;

		ApplicationSpec m_Spec;
		Scope<Window> m_Window;
		// Declared after m_Window so that it is destroyed first: the renderer is bound to the
		// window, which must outlive it.
		Scope<ImGuiRenderer> m_Gui;
		LayerStack m_Layers;
		std::vector<Layer*> m_LayersThisFrame;
		bool m_Running = false;
		bool m_Minimized = false;
		FramePacer m_Pacer;
		bool m_EventArrived = false;
		double m_LastFrameStartSeconds = 0.0;

		static Application* s_Instance;
	};

	Scope<Application> CreateApplication(ApplicationCommandLineArgs args);
}