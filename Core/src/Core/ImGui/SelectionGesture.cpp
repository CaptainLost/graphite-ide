#include "corepch.h"

#include "Core/ImGui/SelectionGesture.h"

#include "Core/ImGui/ContextMenu.h"

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
		const bool actsOnCharacterUnderPointer =
			action == SelectionAction::SelectWord || action == SelectionAction::SelectLine;

		if (actsOnCharacterUnderPointer && source.CharacterAt)
		{
			if (std::optional<TextPoint> characterUnderPointer = source.CharacterAt(screen))
			{
				return characterUnderPointer;
			}
		}

		return boundary;
	}

	namespace {
		constexpr const char* kScrollStepSample = "0";
		constexpr std::size_t kLineStartColumn = 0;
		constexpr std::size_t kUnknownLineLength = 0;
		constexpr bool kKeyRepeatCounts = false;
		constexpr bool kMenuItemIsSelected = false;

		std::optional<TextPoint> PointUnder(const SelectionSource& source, ImVec2 mousePosition)
		{
			return source.PointAt ? source.PointAt(mousePosition) : std::nullopt;
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

		void CopySelectedText(const SelectionSource& source, SelectionGestureResult& result)
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
		const ImGuiIO& inputState = ImGui::GetIO();
		const ImVec2 mousePosition = ImGui::GetMousePos();

		const bool claimed =
			ImGui::IsAnyItemActive() || (source.Claims && source.Claims(mousePosition));

		if (!claimed && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			// WARNING: a child nothing focused never sees a key, so without this the copy
			// shortcut goes wherever the focus happened to be.
			ImGui::SetWindowFocus();

			const std::optional<TextPoint> boundary = PointUnder(source, mousePosition);
			const GestureInput pressInput{ImGui::GetMouseClickedCount(ImGuiMouseButton_Left),
										  inputState.KeyShift, boundary.has_value(),
										  !selection.IsEmpty()};
			const SelectionAction action = DecidePress(pressInput);

			const std::optional<TextPoint> point =
				PressPoint(action, source, mousePosition, boundary);

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
				const ColumnSpan wordSpan = source.WordAt ? source.WordAt(*point) : ColumnSpan{};
				selection.Select({point->Line, wordSpan.From}, {point->Line, wordSpan.To});
				break;
			}
			case SelectionAction::SelectLine:
			{
				const std::size_t lineLength =
					source.LineLength ? source.LineLength(point->Line) : kUnknownLineLength;
				selection.Select({point->Line, kLineStartColumn}, {point->Line, lineLength});
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
				if (const std::optional<TextPoint> pointUnderMouse = PointUnder(source, mousePosition))
				{
					selection.ExtendTo(*pointUnderMouse);
				}

				if (options.ScrollsVertically)
				{
					const float top = ImGui::GetWindowPos().y;
					const float bottom = top + ImGui::GetWindowSize().y;
					const float scrollStep = ImGui::GetTextLineHeightWithSpacing();

					if (mousePosition.y < top)
					{
						ImGui::SetScrollY(ImGui::GetScrollY() - scrollStep);
					}
					else if (mousePosition.y > bottom)
					{
						ImGui::SetScrollY(ImGui::GetScrollY() + scrollStep);
					}
				}

				if (options.ScrollsHorizontally)
				{
					const float left = ImGui::GetWindowPos().x;
					const float right = left + ImGui::GetWindowSize().x;
					const float scrollStep = ImGui::CalcTextSize(kScrollStepSample).x;

					if (mousePosition.x < left)
					{
						ImGui::SetScrollX(ImGui::GetScrollX() - scrollStep);
					}
					else if (mousePosition.x > right)
					{
						ImGui::SetScrollX(ImGui::GetScrollX() + scrollStep);
					}
				}
			}
		}

		if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive() && inputState.KeyCtrl && !inputState.KeyAlt)
		{
			// WARNING: Ctrl+Alt is AltGr on some layouts (Polish letters), so it is not a shortcut.
			if (options.CopyKey && ImGui::IsKeyPressed(ImGuiKey_C, kKeyRepeatCounts))
			{
				CopySelectedText(source, result);
			}
			else if (options.SelectAllKey && ImGui::IsKeyPressed(ImGuiKey_A, kKeyRepeatCounts))
			{
				SelectEverything(selection, source);
			}
		}

		const bool clickedHere = ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
								 ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);

		if (options.MenuId != nullptr && BeginRightClickMenu(options.MenuId, clickedHere))
		{
			if (ImGui::MenuItem("Copy", "Ctrl+C", kMenuItemIsSelected, !selection.IsEmpty()))
			{
				CopySelectedText(source, result);
				result.FromMenu = result.Copied;
			}

			bool hasAnythingToSelect = false;

			if (source.Everything)
			{
				const auto [from, to] = source.Everything();
				hasAnythingToSelect = from != to;
			}

			const char* selectAllShortcut = options.SelectAllKey ? "Ctrl+A" : nullptr;

			if (ImGui::MenuItem("Select All", selectAllShortcut, kMenuItemIsSelected,
								hasAnythingToSelect))
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

		const ImU32 fillColour =
			colour.has_value() ? *colour : ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
		drawList.AddRectFilled(ImVec2(fromX, top), ImVec2(toX, top + height),
							   fillColour);
	}
}
