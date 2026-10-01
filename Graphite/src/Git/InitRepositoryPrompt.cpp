#include "Git/InitRepositoryPrompt.h"

#include "Git/GitModel.h"

#include "Core/ImGui/EmptyState.h"

#include <imgui.h>

#include <string>

namespace Graphite {

	void DrawInitRepositoryPrompt(GitModel& model)
	{
		std::string message = "This folder is not a git repository.";

		if (const auto& error = model.GetWriteError(); error.has_value())
		{
			message += "\n\nCould not initialise it: " + error->Message;
		}

		ImGui::BeginDisabled(model.IsWriteInFlight());

		if (Core::EmptyState(message, "Initialize Repository"))
		{
			model.DismissWriteError();
			model.RequestInit();
		}

		ImGui::EndDisabled();
	}
}
