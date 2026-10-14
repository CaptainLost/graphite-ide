#include "Git/InitRepositoryPrompt.h"

#include "Git/GitModel.h"

#include "Core/Base/Result.h"
#include "Core/ImGui/EmptyState.h"

#include <imgui.h>

#include <optional>
#include <string>

namespace Graphite {

	void DrawInitRepositoryPrompt(GitModel& model)
	{
		std::string message = "This folder is not a git repository.";

		const std::optional<Core::Error>& initError = model.GetInitError();

		if (initError.has_value())
		{
			message += "\n\nCould not initialize it: " + initError->Message;
		}

		ImGui::BeginDisabled(model.IsWriteInFlight() || model.IsAwaitingStatusAfterInit());

		if (Core::EmptyState(message, "Initialize Repository"))
		{
			model.DismissInitError();
			model.RequestInit();
		}

		ImGui::EndDisabled();
	}
}
