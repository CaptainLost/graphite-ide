#include "Hex/HexView.h"

#include "Hex/HexFile.h"
#include "Hex/HexGrid.h"
#include "Hex/HexText.h"
#include "UI/WheelNotches.h"

#include "Core/App/Redraw.h"
#include "Core/Base/Assert.h"
#include "Core/Base/ByteSize.h"
#include "Core/Base/KeyCodes.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/ImGui/Scrollbar.h"
#include "Core/ImGui/UiScale.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	namespace {
		constexpr const char* kRowsId = "##hexRows";
		constexpr const char* kScrollbarId = "##hexScrollbar";
		constexpr const char* kReadingText = "Reading…";
		constexpr const char* kRetryLabel = "Retry";
		constexpr float kHalf = 0.5f;
		constexpr float kSmallestItemSide = 1.0f;
		constexpr float kNoItemSpacing = 0.0f;
		constexpr float kHeaderRuleThickness = 1.0f;
		constexpr std::int64_t kWheelRows = 3;
		constexpr std::int64_t kOneRow = 1;
		constexpr std::size_t kValueBytes = 4;

		// WARNING: NoNavFocus is needed as much as the items' NoNav. A click otherwise makes the rows or the bar ImGui's
		// navigation focus, and Space or Enter then presses them as if at the pointer, wherever it is: the selection
		// jumps to the byte nearest it and, past the rows' edge, the rows scroll for as long as the key is held. The
		// window is still focused by the click.
		constexpr ImGuiButtonFlags kPointerOnly = ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_NoNavFocus;

		struct KeyBinding
		{
			Core::KeyCode Key = Core::KeyCode::Left;
			HexMove Plain = HexMove::Left;
			std::optional<HexMove> WithControl;
		};

		constexpr std::array kKeyBindings{
			KeyBinding{Core::KeyCode::Left, HexMove::Left, std::nullopt},
			KeyBinding{Core::KeyCode::Right, HexMove::Right, std::nullopt},
			KeyBinding{Core::KeyCode::Up, HexMove::Up, std::nullopt},
			KeyBinding{Core::KeyCode::Down, HexMove::Down, std::nullopt},
			KeyBinding{Core::KeyCode::PageUp, HexMove::PageUp, std::nullopt},
			KeyBinding{Core::KeyCode::PageDown, HexMove::PageDown, std::nullopt},
			KeyBinding{Core::KeyCode::Home, HexMove::RowStart, HexMove::FileStart},
			KeyBinding{Core::KeyCode::End, HexMove::RowEnd, HexMove::FileEnd},
		};

		struct KeyMove
		{
			HexMove Move = HexMove::Left;
			bool Extend = false;
		};

		struct Scrollbar
		{
			ImRect Rail;
			ScrollGrab Grab;
			bool OverGrab = false;
			bool Held = false;
		};

		// Shift extends any move; Control is part of only Ctrl+Home and Ctrl+End, and any other modifier leaves the
		// stroke to someone else.
		std::optional<KeyMove> MoveFor(const Core::KeyStroke& stroke)
		{
			const bool extend = Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Shift);
			const auto others = static_cast<Core::KeyModifier>(static_cast<std::uint8_t>(stroke.Modifiers) &
															   ~static_cast<std::uint8_t>(Core::KeyModifier::Shift));
			const bool control = others == Core::KeyModifier::Control;

			if (others != Core::KeyModifier::None && !control)
			{
				return std::nullopt;
			}

			for (const KeyBinding& binding : kKeyBindings)
			{
				if (binding.Key != stroke.Key)
				{
					continue;
				}

				if (control)
				{
					return binding.WithControl ? std::optional<KeyMove>(KeyMove{*binding.WithControl, extend}) : std::nullopt;
				}

				return KeyMove{binding.Plain, extend};
			}

			return std::nullopt;
		}

		void FollowChanges(const HexFile& file, HexViewState& state)
		{
			if (file.GetChangeCount() == state.SeenChange)
			{
				return;
			}

			state.Selection = Clamped(state.Selection, file.GetSize());
			state.SeenChange = file.GetChangeCount();
		}

		void DrawNotice(std::string_view notice)
		{
			const ImGuiWindow* const window = ImGui::GetCurrentWindow();
			const float left = window->InnerRect.Min.x + Core::Scaled(kBreadcrumbLeftPadding);
			ImGui::SetCursorScreenPos(ImVec2(left, ImGui::GetCursorScreenPos().y));

			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextUnformatted(notice.data(), notice.data() + notice.size());
			ImGui::PopStyleColor();
		}

		// A rule along the header's bottom edge, over the scrollbar's strip too, since the bar starts below it.
		void DrawHeaderRule(ImDrawList& drawList, const ImRect& header)
		{
			drawList.AddRectFilled(ImVec2(header.Min.x, header.Max.y - kHeaderRuleThickness), header.Max,
								   ImGui::GetColorU32(ImGuiCol_Separator));
		}

		// One line, after the rows' header, laid out without the spacing that would part it from the rows.
		void ReserveLine(float height)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, kNoItemSpacing));
			ImGui::Dummy(ImVec2(std::max(0.0f, ImGui::GetContentRegionAvail().x), height));
			ImGui::PopStyleVar();
		}

		// The loader is in the interface font, so it is centred on the header's line rather than set on it. It stands at
		// the header's right end after the last heading, and is left out where it would not fit there: the placeholders
		// show the wait.
		void DrawReading(const ImRect& header, float headingsEnd)
		{
			const float padding = Core::Scaled(kBreadcrumbLeftPadding);
			const float right = header.Max.x - padding;
			const float left = right - Core::LoadingLineWidth(kReadingText);

			if (left < headingsEnd + padding)
			{
				return;
			}

			const float top = header.Min.y + std::floor((header.GetHeight() - ImGui::GetTextLineHeight()) * kHalf);
			ImGui::SetCursorScreenPos(ImVec2(left, top));
			Core::LoadingLine(kReadingText, Core::WhileLoaderWaits::TakeNoRoom);
		}

		void WantRowsOnScreen(HexFile& file, const HexScroll& scroll, const HexRows& rows)
		{
			const std::uint64_t size = file.GetSize();
			const std::uint64_t first = scroll.TopRow * kBytesPerRow;
			const std::uint64_t end = std::min(size, rows.EndRow(scroll) * kBytesPerRow);

			if (end > first)
			{
				file.Want(first, end - 1);
			}
		}

		void ApplyKeys(const Core::FrameInput& keys, HexViewState& state, std::uint64_t size, const HexRows& rows)
		{
			for (const Core::KeyStroke& stroke : keys.GetStrokes())
			{
				const std::optional<KeyMove> move = MoveFor(stroke);

				if (!move)
				{
					continue;
				}

				state.Selection = Moved(state.Selection, move->Move, move->Extend, rows.Visible, size);

				// A page turns the rows with the selection, so the active byte stays where it stood on screen.
				if (move->Move == HexMove::PageUp || move->Move == HexMove::PageDown)
				{
					const auto page = static_cast<std::int64_t>(rows.Visible);
					state.Scroll =
						ScrolledBy(state.Scroll, move->Move == HexMove::PageUp ? -page : page, rows.Count, rows.Visible);
				}

				state.Scroll = Revealing(state.Scroll, state.Selection.Active / kBytesPerRow, rows.Visible, rows.Count);
			}
		}

		void ApplyWheel(ImGuiID owner, HexViewState& state, const HexRows& rows)
		{
			// WARNING: The wheel is claimed on every hovered frame, not only when a notch arrives. ImGui applies the
			// wheel to the window's own scroll at the start of the next frame and tests the ownership set during this one.
			ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, owner);

			if (const int notches = TakeWholeNotches(state.WheelPending, ImGui::GetIO().MouseWheel); notches != 0)
			{
				state.Scroll = ScrolledBy(state.Scroll, -static_cast<std::int64_t>(notches) * kWheelRows, rows.Count,
										  rows.Visible);
			}
		}

		// The grab is held by where it was taken, kept in the window's storage under the bar's id for the drag. A press on
		// the track beside the grab brings the grab's middle to the pointer.
		Scrollbar UpdateScrollbar(HexViewState& state, const HexRows& rows)
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const ImGuiID id = ImGui::GetID(kScrollbarId);
			ImGui::ItemAdd(rows.Track, id, nullptr, ImGuiItemFlags_NoNav);

			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(rows.Track, id, &hovered, &held, kPointerOnly);

			const float inset = Core::ScrollbarGrabInset(style.ScrollbarPadding, rows.Track.GetSize());
			Scrollbar bar{.Rail = ImRect(ImVec2(rows.Track.Min.x + inset, rows.Track.Min.y + inset),
										 ImVec2(rows.Track.Max.x - inset, rows.Track.Max.y - inset)),
						  .Held = held};
			const float travel = bar.Rail.GetHeight();
			bar.Grab = GrabFor(state.Scroll, rows.Count, rows.Visible, travel, style.GrabMinSize);

			const ImGuiIO& io = ImGui::GetIO();
			const float mouseY = io.MousePos.y;
			const float grabTop = bar.Rail.Min.y + bar.Grab.Top;
			const bool onGrab = mouseY >= grabTop && mouseY < grabTop + bar.Grab.Height;
			ImGuiStorage& storage = *ImGui::GetStateStorage();

			if (pressed)
			{
				storage.SetFloat(id, onGrab ? mouseY - grabTop : bar.Grab.Height * kHalf);
			}

			// A grab taken where it stands keeps its row until the pointer moves; a press beside it jumps there at once.
			if (held && (io.MouseDelta.y != 0.0f || (pressed && !onGrab)))
			{
				state.Scroll = ScrollForGrabTop(mouseY - bar.Rail.Min.y - storage.GetFloat(id), rows.Count, rows.Visible,
												travel, bar.Grab.Height);
				bar.Grab = GrabFor(state.Scroll, rows.Count, rows.Visible, travel, style.GrabMinSize);
			}

			bar.OverGrab = hovered && mouseY >= bar.Rail.Min.y + bar.Grab.Top &&
						   mouseY < bar.Rail.Min.y + bar.Grab.Top + bar.Grab.Height;
			return bar;
		}

		void DrawScrollbar(ImDrawList& drawList, const Scrollbar& bar, const HexRows& rows)
		{
			if (rows.Count <= rows.Visible)
			{
				return;
			}

			const ImGuiCol colour = bar.Held	   ? ImGuiCol_ScrollbarGrabActive
									: bar.OverGrab ? ImGuiCol_ScrollbarGrabHovered
												   : ImGuiCol_ScrollbarGrab;

			drawList.AddRectFilled(ImVec2(bar.Rail.Min.x, bar.Rail.Min.y + bar.Grab.Top),
								   ImVec2(bar.Rail.Max.x, bar.Rail.Min.y + bar.Grab.Top + bar.Grab.Height),
								   ImGui::GetColorU32(colour), ImGui::GetStyle().ScrollbarRounding);
		}

		// A press selects the byte nearest the pointer, or extends to it with Shift; while it is held the active end
		// follows the pointer, and past the rows' top or bottom edge the rows scroll a row a frame.
		void ApplyPointer(HexViewState& state, std::uint64_t size, const HexGrid& grid, const HexRows& rows, bool pressed,
						  bool held)
		{
			if (!held || size == 0)
			{
				return;
			}

			const ImVec2 pointer = ImGui::GetIO().MousePos;

			if (!pressed && (pointer.y < rows.Area.Min.y || pointer.y >= rows.Area.Max.y))
			{
				state.Scroll = ScrolledBy(state.Scroll, pointer.y < rows.Area.Min.y ? -kOneRow : kOneRow, rows.Count,
										  rows.Visible);
				Core::Redraw::Request();
			}

			const std::uint64_t at = NearestByte(grid, rows, state.Scroll, size, pointer);
			state.Selection.Active = at;

			if (pressed && !ImGui::GetIO().KeyShift)
			{
				state.Selection.Anchor = at;
			}
		}

		void DrawRowsRegion(HexFile& file, HexViewState& state, const HexViewInput& input, const HexGrid& grid,
							const HexRows& rows, bool holdsKeyboard)
		{
			const std::uint64_t size = file.GetSize();
			const ImGuiID rowsId = ImGui::GetID(kRowsId);

			if (!ImGui::ItemAdd(rows.Area, rowsId, nullptr, ImGuiItemFlags_NoNav))
			{
				return;
			}

			bool hovered = false;
			bool held = false;
			const bool pressed = ImGui::ButtonBehavior(rows.Area, rowsId, &hovered, &held, kPointerOnly);

			if (holdsKeyboard && input.Keys != nullptr)
			{
				ApplyKeys(*input.Keys, state, size, rows);
			}

			const ImRect whole(rows.Area.Min, rows.Track.Max);

			const ImGuiIO& io = ImGui::GetIO();

			if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
				ImGui::IsMouseHoveringRect(whole.Min, whole.Max))
			{
				// With Control held the turn is not the view's: the rows are drawn in the editor font, which it zooms.
				if (io.KeyCtrl)
				{
					state.WheelPending = 0.0f;

					if (io.MouseWheel != 0.0f && input.RouteWheel)
					{
						input.RouteWheel();
					}
				}
				else
				{
					ApplyWheel(rowsId, state, rows);
				}
			}

			const Scrollbar bar = UpdateScrollbar(state, rows);
			ApplyPointer(state, size, grid, rows, pressed, held);

			state.Hovered = hovered ? ByteUnder(grid, rows, state.Scroll, size, ImGui::GetIO().MousePos) : std::nullopt;

			if (state.Hovered)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
			}
			else
			{
				state.WheelPending = 0.0f;
			}

			WantRowsOnScreen(file, state.Scroll, rows);

			ImDrawList& drawList = *ImGui::GetWindowDrawList();
			DrawRows(drawList, file, state, *input.Palette, grid, rows);
			DrawScrollbar(drawList, bar, rows);
		}

		// The status bar's values come from the selection's first bytes, which may be off screen or run on into a block
		// whose rows are not, so they are asked for every frame as the rows are: at most two blocks.
		void WantSelectedValues(HexFile& file, const HexSelection& selection)
		{
			const std::uint64_t size = file.GetSize();

			if (size > 0)
			{
				file.Want(selection.First(), std::min(selection.First() + (kValueBytes - 1), size - 1));
			}
		}

		std::vector<ViewStatusItem> StatusOf(const HexFile& file, const HexSelection& selection)
		{
			const std::uint64_t size = file.GetSize();

			if (size == 0)
			{
				return {ViewStatusItem{Core::FormatByteSize(size), false, std::nullopt}};
			}

			// The values are left out while the selected bytes are still being read, rather than shown empty.
			std::vector<ViewStatusItem> items = HexStatusItems(selection.First(), selection.Count(),
															   file.BytesFrom(selection.First(), kValueBytes), size);
			std::erase_if(items, [](const ViewStatusItem& item) { return item.Text.empty(); });
			return items;
		}
	}

	HexViewFrame DrawHexView(HexFile& file, HexViewState& state, const HexViewInput& input)
	{
		CORE_ASSERT(input.Path != nullptr, "a hex view draws its path row");
		CORE_ASSERT(input.Palette != nullptr, "a hex view draws in the editor's palette");

		HexViewFrame frame;
		frame.Focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);

		// WARNING: A popup does not count: with a menu open, a stroke belongs to the menu, not to the tab behind it.
		const bool holdsKeyboard =
			ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows | ImGuiFocusedFlags_NoPopupHierarchy);

		FollowChanges(file, state);
		state.Hovered.reset();

		if (ImGui::GetCurrentWindow()->SkipItems)
		{
			WantSelectedValues(file, state.Selection);
			frame.Status = StatusOf(file, state.Selection);
			return frame;
		}

		DrawBreadcrumb(*input.Path);

		if (!input.Notice.empty())
		{
			DrawNotice(input.Notice);
		}

		ImFont* const codeFace = Core::ImGuiRenderer::GetMonoFont();
		const ImGuiWindow* const window = ImGui::GetCurrentWindow();
		const float barWidth = ImGui::GetStyle().ScrollbarSize;

		ImGui::PushFont(codeFace, input.FontSize);
		const HexGrid grid = MeasureGrid(window->InnerRect.Min.x + Core::Scaled(kBreadcrumbLeftPadding), file.GetSize());
		const float headerTop = ImGui::GetCursorScreenPos().y;
		const ImRect header(ImVec2(window->InnerRect.Min.x, headerTop),
							ImVec2(window->InnerRect.Max.x, headerTop + grid.LineHeight));
		DrawHeader(*ImGui::GetWindowDrawList(), grid, ImRect(header.Min, ImVec2(header.Max.x - barWidth, header.Max.y)));
		DrawHeaderRule(*ImGui::GetWindowDrawList(), header);
		ReserveLine(grid.LineHeight);
		ImGui::PopFont();

		const ImVec2 rowsCursor = ImGui::GetCursorScreenPos();

		// Read before this frame asks for its rows, so it tells of the reads the frame before left running.
		if (file.IsWaiting())
		{
			DrawReading(header, grid.HeaderRight());
			ImGui::SetCursorScreenPos(rowsCursor);
		}

		// WARNING: The region is laid out at the size of the content region and takes the whole inner rectangle only as
		// the rectangle it answers to. Laid out that wide it would reach past the window's padding, and the window would
		// grow scrollbars. A region with no room left is still one pixel, as a zero size is refused.
		const ImVec2 available = ImGui::GetContentRegionAvail();
		const ImRect area(ImVec2(window->InnerRect.Min.x, rowsCursor.y),
						  ImVec2(window->InnerRect.Max.x, std::max(window->InnerRect.Max.y, rowsCursor.y + kSmallestItemSide)));
		const HexRows rows = LayOutRows(area, std::min(barWidth, area.GetWidth()), grid, file.GetSize());
		state.Scroll = ScrolledBy(state.Scroll, 0, rows.Count, rows.Visible);

		if (!file.GetFailure().empty())
		{
			// Still asked for, so that Retry reads them again.
			WantRowsOnScreen(file, state.Scroll, rows);

			if (Core::EmptyState(file.GetFailure(), kRetryLabel))
			{
				file.Retry();
			}
		}
		else
		{
			ImGui::PushFont(codeFace, input.FontSize);
			ImGui::ItemSize(ImVec2(std::max(available.x, kSmallestItemSide), std::max(available.y, kSmallestItemSide)));
			DrawRowsRegion(file, state, input, grid, rows, holdsKeyboard);
			ImGui::PopFont();
		}

		WantSelectedValues(file, state.Selection);
		frame.Status = StatusOf(file, state.Selection);
		return frame;
	}
}
