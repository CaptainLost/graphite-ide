#include "corepch.h"

#include "Core/ImGui/SelectionGesture.h"

namespace Core {

	SelectionAction DecidePress(const GestureInput& input)
	{
		if (input.ClickCount <= 0)
		{
			return SelectionAction::None;
		}

		if (!input.OverLine)
		{
			return SelectionAction::Clear;
		}

		if (input.ClickCount >= 3)
		{
			return SelectionAction::SelectLine;
		}

		if (input.ClickCount == 2)
		{
			return SelectionAction::SelectWord;
		}

		return input.Shift && input.HasSelection ? SelectionAction::Extend : SelectionAction::Begin;
	}

	std::optional<TextPoint> PressPoint(SelectionAction action, const SelectionSource& source,
										ImVec2 screen, std::optional<TextPoint> boundary)
	{
		const bool character = action == SelectionAction::SelectWord || action == SelectionAction::SelectLine;

		if (character && source.CharacterAt)
		{
			if (std::optional<TextPoint> under = source.CharacterAt(screen))
			{
				return under;
			}
		}

		return boundary;
	}

	namespace {
		std::optional<TextPoint> PointUnder(const SelectionSource& source, ImVec2 mouse)
		{
			return source.PointAt ? source.PointAt(mouse) : std::nullopt;
		}

		void SelectEverything(TextSelection& selection, const SelectionSource& source)
		{
			if (!source.Everything)
			{
				return;
			}

			const auto [from, to] = source.Everything();
			selection.Select(from, to);
		}

		void Copy(const SelectionSource& source, SelectionGestureResult& result)
		{
			if (!source.SelectedText)
			{
				return;
			}

			std::string text = source.SelectedText();

			if (text.empty())
			{
				return;
			}

			ImGui::SetClipboardText(text.c_str());
			result.Copied = true;
			result.CopiedText = std::move(text);
		}
	}

	SelectionGestureResult SelectionGesture::Update(TextSelection& selection,
													const SelectionSource& source,
													const SelectionGestureOptions& options)
	{
		SelectionGestureResult result;
		const ImGuiIO& io = ImGui::GetIO();
		const ImVec2 mouse = ImGui::GetMousePos();

		const bool claimed =
			ImGui::IsAnyItemActive() || (source.Claims && source.Claims(mouse));

		if (!claimed && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			// A child nothing focused never sees a key: without this the copy shortcut would
			// go wherever the focus happened to be.
			ImGui::SetWindowFocus();

			const std::optional<TextPoint> boundary = PointUnder(source, mouse);
			const SelectionAction action =
				DecidePress({ImGui::GetMouseClickedCount(ImGuiMouseButton_Left), io.KeyShift,
							 boundary.has_value(), !selection.IsEmpty()});

			// A word or a line is the one under the pointer, not the one past the nearest
			// boundary; a sweep and a single press keep the boundary.
			const std::optional<TextPoint> point = PressPoint(action, source, mouse, boundary);

			// Every press settles the sweep first, so no gesture order leaves it stuck.
			m_Sweeping = false;

			switch (action)
			{
			case SelectionAction::Begin:
				selection.Begin(*point);
				m_Sweeping = true;
				break;
			case SelectionAction::Extend:
				selection.ExtendTo(*point);
				m_Sweeping = true;
				break;
			case SelectionAction::SelectWord:
			{
				const ColumnSpan span = source.WordAt ? source.WordAt(*point) : ColumnSpan{};
				selection.Select({point->Line, span.From}, {point->Line, span.To});
				break;
			}
			case SelectionAction::SelectLine:
			{
				const std::size_t length = source.LineLength ? source.LineLength(point->Line) : 0;
				selection.Select({point->Line, 0}, {point->Line, length});
				break;
			}
			case SelectionAction::Clear:
				selection.Clear();
				break;
			case SelectionAction::None:
				break;
			}
		}

		if (m_Sweeping)
		{
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				m_Sweeping = false;
			}
			else
			{
				if (const std::optional<TextPoint> point = PointUnder(source, mouse))
				{
					selection.ExtendTo(*point);
				}

				// Dragging out of the top or the bottom scrolls after the pointer, so a
				// selection can reach further than the window is tall.
				if (options.ScrollsVertically)
				{
					const float top = ImGui::GetWindowPos().y;
					const float bottom = top + ImGui::GetWindowSize().y;
					const float step = ImGui::GetTextLineHeightWithSpacing();

					if (mouse.y < top)
					{
						ImGui::SetScrollY(ImGui::GetScrollY() - step);
					}
					else if (mouse.y > bottom)
					{
						ImGui::SetScrollY(ImGui::GetScrollY() + step);
					}
				}

				if (options.ScrollsHorizontally)
				{
					const float left = ImGui::GetWindowPos().x;
					const float right = left + ImGui::GetWindowSize().x;
					const float step = ImGui::CalcTextSize("0").x;

					if (mouse.x < left)
					{
						ImGui::SetScrollX(ImGui::GetScrollX() - step);
					}
					else if (mouse.x > right)
					{
						ImGui::SetScrollX(ImGui::GetScrollX() + step);
					}
				}
			}
		}

		if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && io.KeyCtrl && !io.KeyAlt)
		{
			// Ctrl+Alt is AltGr on some layouts (Polish letters), so it is not a shortcut.
			if (options.CopyKey && ImGui::IsKeyPressed(ImGuiKey_C, false))
			{
				Copy(source, result);
			}
			else if (options.SelectAllKey && ImGui::IsKeyPressed(ImGuiKey_A, false))
			{
				SelectEverything(selection, source);
			}
		}

		if (options.MenuId != nullptr && ImGui::BeginPopupContextWindow(options.MenuId))
		{
			if (ImGui::MenuItem("Copy", "Ctrl+C", false, !selection.IsEmpty()))
			{
				Copy(source, result);
			}

			bool anything = false;

			if (source.Everything)
			{
				const auto [from, to] = source.Everything();
				anything = from != to;
			}

			if (ImGui::MenuItem("Select All", options.SelectAllKey ? "Ctrl+A" : nullptr, false,
								anything))
			{
				SelectEverything(selection, source);
			}

			ImGui::EndPopup();
		}

		return result;
	}

	void DrawSelectionSpan(ImDrawList& drawList, float fromX, float toX, float top, float height,
						   std::optional<ImU32> colour)
	{
		if (toX <= fromX)
		{
			return;
		}

		drawList.AddRectFilled(ImVec2(fromX, top), ImVec2(toX, top + height),
							   colour.has_value() ? *colour : ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
	}
}
