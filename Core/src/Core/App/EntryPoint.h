#pragma once

#include "Core/App/Application.h"
#include "Core/Base/Base.h"

#include <exception>

#ifdef CORE_PLATFORM_WINDOWS

extern Core::Scope<Core::Application> Core::CreateApplication(Core::ApplicationCommandLineArgs args);

int main(int argc, char** argv)
{
	// First, before anything the client does: until the sinks are attached a message has
	// nowhere to go, and the startup path is where the messages worth keeping are.
	Core::Log::Init(Core::CreateLogSpec());

	try
	{
		Core::Scope<Core::Application> app = Core::CreateApplication({argc, argv});
		if (!app)
		{
			CORE_ERROR("Startup aborted: the client created no application");
			return 1;
		}

		// Run reports failure as a value rather than throwing, so startup errors — no
		// window, no ImGui context — arrive here as an exit code and a logged reason
		// rather than as a crash the user has to interpret.
		if (const auto result = app->Run(); !result)
		{
			CORE_ERROR("Application terminated: {}", result.GetError().Message);
			return 1;
		}

		return 0;
	}
	catch (const std::exception& e)
	{
		CORE_ERROR("Unhandled exception: {}", e.what());
		return 1;
	}
	catch (...)
	{
		CORE_ERROR("Unhandled unknown exception");
		return 1;
	}
}

#endif
