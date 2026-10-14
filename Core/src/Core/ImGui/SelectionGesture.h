#pragma once

#include "Core/Text/TextSelection.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace Core {

	enum class SelectionAction
	{
		None,
		Begin,
		Extend,
		SelectWord,
		SelectLine,
		Clear
	};

	struct GestureInput
	{
		int ClickCount = 0;
		bool Shift = false;
		bool OverLine = false;
		bool HasSelection = false;
	};

	SelectionAction DecidePress(const GestureInput& input);

	struct SelectionSource;

	std::optional<TextPoint> PressPoint(SelectionAction action, const SelectionSource& source,
										ImVec2 screen, std::optional<TextPoint> boundary);

	struct SelectionSource
	{
		std::function<std::optional<TextPoint>(ImVec2 screen)> PointAt;
		std::function<std::optional<TextPoint>(ImVec2 screen)> CharacterAt;
		std::function<ColumnSpan(TextPoint at)> WordAt;
		std::function<std::size_t(std::int64_t line)> LineLength;
		std::function<std::pair<TextPoint, TextPoint>()> Everything;
		std::function<std::string()> SelectedText;
		std::function<bool(ImVec2 screen)> Claims;
	};

	struct SelectionGestureResult
	{
		bool Copied = false;
		std::string CopiedText;

		bool FromMenu = false;
	};

	struct SelectionGestureOptions
	{
		bool ScrollsVertically = true;
		bool ScrollsHorizontally = false;
		bool CopyKey = true;
		bool SelectAllKey = true;
		const char* MenuId = nullptr;
	};

	class SelectionGesture
	{
	public:
		SelectionGestureResult Update(TextSelection& selection, const SelectionSource& source,
									  const SelectionGestureOptions& options);

		bool IsSweeping() const
		{
			return m_Sweeping;
		}

		void Cancel()
		{
			m_Sweeping = false;
		}

	private:
		bool m_Sweeping = false;
	};

	void DrawSelectionSpan(ImDrawList& drawList, float fromX, float toX, float top, float height,
						   std::optional<ImU32> colour = std::nullopt);
}
