#include "corepch.h"

#include "Core/ImGui/DisabledOffer.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Core {

	namespace {

		bool IsLastItemDisabled()
		{
			return (ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0;
		}
	}

	bool OfferWhileDisabled(const char* note)
	{
		if (!IsLastItemDisabled())
		{
			return false;
		}

		ImGui::SetItemTooltip("%s", note);

		const bool pointedAt = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
		return pointedAt && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	}
}
