#pragma once

#include "Core/App/Application.h"
#include "Core/Base/Base.h"

#include <cstdlib>
#include <exception>

#ifdef CORE_PLATFORM_WINDOWS

extern Core::Scope<Core::Application> Core::CreateApplication(Core::ApplicationCommandLineArgs args);

int main(int argumentCount, char** arguments)
{
	// Before anything the client does: until the sinks are attached a message has nowhere to
	// go, and the startup path is where the messages worth keeping are.
	Core::Log::Init(Core::CreateLogSpec());

	try
	{
		Core::Scope<Core::Application> application = Core::CreateApplication(Core::ApplicationCommandLineArgs{.Count = argumentCount, .Args = arguments});
		if (application == nullptr)
		{
			CORE_ERROR("Startup aborted: the client created no application");
			return EXIT_FAILURE;
		}

		if (const Core::Result<void> runResult = application->Run(); !runResult)
		{
			CORE_ERROR("Application terminated: {}", runResult.GetError().Message);
			return EXIT_FAILURE;
		}

		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		CORE_ERROR("Unhandled exception: {}", error.what());
		return EXIT_FAILURE;
	}
	catch (...)
	{
		CORE_ERROR("Unhandled unknown exception");
		return EXIT_FAILURE;
	}
}

#endif
