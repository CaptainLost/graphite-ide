#include "Core/Base/Base.h"

#include "Panels/CostView.h"

#include <imgui.h>

namespace Graphite {

	// Task 12 replaces this with the cost chart and totals; for now the Cost tab exists and
	// says why it is empty.
	void DrawCost(const Session& /*session*/, const std::optional<SessionSummary>& /*summary*/,
				 const Pricing& /*pricing*/)
	{
		ImGui::TextDisabled("Cost view comes next.");
	}
}
