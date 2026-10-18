#include "Explorer/RowRenameField.h"

#include "UI/Lists/ListRow.h"

#include "Core/ImGui/UiScale.h"

#include <imgui.h>

#include <algorithm>

namespace Graphite {

	namespace {
		constexpr float kFieldPaddingX = 2.0f;
		constexpr float kMinFieldWidth = 90.0f;

		float FitWidthToText(const char* text, float paddingX)
		{
			const float caretSlack = ImGui::GetFontSize();
			const float fittedWidth = ImGui::CalcTextSize(text).x + paddingX * 2.0f + caretSlack;

			// WARNING: Not clamp: a deep row in a narrow panel has less room than the least width, and clamp's bounds
			// would be the wrong way round.
			const float room = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
			return std::min(std::max(fittedWidth, Core::Scaled(kMinFieldWidth)), room);
		}
	}

	bool RowRenameField::Start(std::string_view name, bool isFolder)
	{
		if (name.size() >= m_Buffer.size())
		{
			return false;
		}

		m_Buffer.fill('\0');
		std::copy_n(name.begin(), name.size(), m_Buffer.begin());
		m_Span = RenameSelection(name, isFolder);
		m_Focus = true;

		return true;
	}

	RenameEnd RowRenameField::Draw(const char* id)
	{
		if (m_Focus)
		{
			ImGui::SetKeyboardFocusHere();
			m_Focus = false;
		}

		const ImVec2 rowStart = ImGui::GetCursorScreenPos();
		const float textOffset = ListRowTextTop(rowStart.y) - rowStart.y;
		const float paddingX = Core::Scaled(kFieldPaddingX);
		const float indentToLabel = ImGui::GetTreeNodeToLabelSpacing() - paddingX;
		const bool isIndented = indentToLabel > 0.0f;

		if (isIndented)
		{
			ImGui::Indent(indentToLabel);
		}

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(paddingX, textOffset));
		ImGui::SetNextItemWidth(FitWidthToText(m_Buffer.data(), paddingX));

		// WARNING: ImGui runs this on every frame the field is active. Only the frame it activates on selects the stem,
		// because AutoSelectAll would take the extension too.
		const auto selectStem = [](ImGuiInputTextCallbackData* data) {
			const RenameSpan& span = *static_cast<const RenameSpan*>(data->UserData);

			if (data->EventActivated)
			{
				const int end = std::min(static_cast<int>(span.End), data->BufTextLen);
				const int start = std::min(static_cast<int>(span.Start), end);
				data->SetSelection(start, end);
			}

			return 0;
		};

		const bool committed =
			ImGui::InputText(id, m_Buffer.data(), m_Buffer.size(),
							 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackAlways, selectStem, &m_Span);

		// WARNING: Escape is read only while this field is focused. An unscoped key poll would also catch an Escape
		// aimed at a modal elsewhere.
		const bool cancelled = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);

		// WARNING: IsItemDeactivated fires whether or not the text changed, which is what ends a rename that was
		// clicked away from instead of leaving the row stuck in the editor.
		const bool deactivated = !cancelled && ImGui::IsItemDeactivated();

		ImGui::PopStyleVar();

		if (isIndented)
		{
			ImGui::Unindent(indentToLabel);
		}

		// WARNING: The field stands in a list row's place, its text on the rows' text line, and is laid out one row
		// tall from the row's top: a frame and the item spacing after it would end the row below the pitch, and the
		// line a row before it left standing would end it elsewhere again.
		ImGui::SetCursorScreenPos(rowStart);
		ImGui::ItemSize(ImVec2(0.0f, ListRowLineHeight()));

		if (cancelled)
		{
			return RenameEnd::Cancelled;
		}

		if (committed || deactivated)
		{
			return RenameEnd::Committed;
		}

		return RenameEnd::StillEditing;
	}
}
