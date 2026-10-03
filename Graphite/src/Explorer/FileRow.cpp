#include "Explorer/FileRow.h"

#include <algorithm>

namespace Graphite {

	FileRowLayout LayoutFileRow(const std::string& label)
	{
		const float labelOffset = ImGui::GetTreeNodeToLabelSpacing();
		const ImVec2 cursor = ImGui::GetCursorScreenPos();

		FileRowLayout layout;
		layout.LabelAt = ImVec2(cursor.x + labelOffset, cursor.y);
		layout.Width =
			std::max(ImGui::GetContentRegionAvail().x, labelOffset + ImGui::CalcTextSize(label.c_str()).x);

		return layout;
	}

	void DrawFileRowLabel(const FileRowLayout& layout, ImU32 ink, const std::string& label)
	{
		if (ImGui::IsItemVisible())
		{
			ImGui::GetWindowDrawList()->AddText(layout.LabelAt, ink, label.c_str());
		}
	}
}
