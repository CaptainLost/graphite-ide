#include "Editor/TextEditorWidget.h"

#include "Core/Base/EmptyVector.h"
#include "Core/Base/Narrow.h"

#include "Editor/Authors/AuthorCard.h"
#include "Editor/Authors/AuthorText.h"
#include "Editor/BracketMatch.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/EditorCommands.h"
#include "Theme/EditorPalette.h"
#include "Editor/EditorWidgetState.h"
#include "Editor/Find/FindBar.h"
#include "Editor/Find/FindNavigation.h"
#include "Editor/Hover/HoverState.h"
#include "Editor/Hover/HoverToken.h"
#include "Editor/Hover/HoverTooltip.h"
#include "Editor/Hover/InteractiveTooltip.h"
#include "Editor/Overlays/LensModel.h"
#include "Editor/Overlays/LensWaiting.h"
#include "Editor/Overlays/ScreenRows.h"
#include "Editor/Find/MatchColumns.h"
#include "Editor/Find/SelectionOccurrences.h"
#include "Theme/Palettes.h"
#include "Editor/Overlays/SemanticOverlay.h"
#include "Git/Changes/ChangeMarkers.h"
#include "Git/Changes/ChangeOverview.h"
#include "Git/Changes/LineSpans.h"

#include "Language/Api/Declarations.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/SyntaxCache.h"

#include "Theme/BuiltInSchemes.h"
#include "UI/LineNumberColumn.h"
#include "UI/TypographicMarks.h"

#include "Core/ImGui/UiScale.h"
#include "Core/App/Redraw.h"
#include "Core/ImGui/EmptyState.h"
#include "Core/ImGui/LoadingState.h"
#include "Core/Command/KeyName.h"
#include "Core/Command/MenuBar.h"
#include "Core/Command/ShortcutRouter.h"
#include "Core/Events/FrameInput.h"
#include "Core/Events/KeyEvent.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/ImGuiRenderer.h"
#include "Core/ImGui/Scrollbar.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Platform/Clipboard.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/Encoding.h"
#include "Core/Text/RowMap.h"
#include "Core/Text/TextChange.h"
#include "Core/Text/TextDocument.h"
#include "Core/Text/WrapLayout.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Graphite {

	namespace {
		constexpr float kStaleDiagnosticAlpha = 0.4f;

		constexpr float kBreakpointColumnCells = 1.0f;

		constexpr float kFoldColumnCells = 1.0f;

		constexpr float kChangeColumnWidth = 5.0f;
		constexpr float kChangeBarWidth = 3.0f;

		constexpr float kStagedChangeAlpha = 0.45f;

		constexpr double kCaretBlinkPeriod = 1.0;
		constexpr double kHalfBlinkPeriod = kCaretBlinkPeriod * 0.5;

		constexpr float kLineHeightFactor = 1.12f;
		constexpr float kLensFontFactor = 0.78f;

		constexpr float kAuthorGapCells = 3.0f;

		constexpr float kPeekInset = 6.0f;
		constexpr float kPeekGap = 2.0f;
		constexpr float kPeekPaddingX = 8.0f;
		constexpr float kPeekPaddingY = 4.0f;

		constexpr float kHalf = 0.5f;
		constexpr float kHairlineWidth = 1.0f;
		constexpr float kLeastWrapColumns = 1.0f;

		constexpr const char* kRetryLabel = "Retry";

		constexpr int kTripleClick = 3;
		constexpr int kDoubleClick = 2;

		constexpr std::size_t kFirstRow = 0;
		constexpr std::size_t kFirstLine = 0;
		constexpr std::size_t kRowStartColumn = 0;
		constexpr std::size_t kOneRow = 1;
		constexpr std::size_t kNewlineCells = 1;

		constexpr unsigned int kFirstPrintableCodepoint = 0x20;
		constexpr unsigned int kDeleteCodepoint = 0x7f;
		constexpr std::size_t kUtf8BufferSize = 5;

		constexpr std::size_t kColumnBeyondAnyRow = std::numeric_limits<std::size_t>::max() / 2;

		constexpr bool kNotRepeating = false;

		enum class RowDirection
		{
			Up,
			Down
		};

		enum class ColumnAim
		{
			KeepAiming,
			StopAiming
		};

		const LensRows& NoLenses()
		{
			static const LensRows rows;

			return rows;
		}

		const LensRows& LensRowsOf(const LensModel* model)
		{
			return model != nullptr ? model->GetRows() : NoLenses();
		}

		std::size_t AdvanceColumns(std::string_view expanded, std::size_t byte,
								   std::size_t columns)
		{
			std::size_t byteOffset = std::min(byte, expanded.size());

			for (std::size_t taken = 0; taken < columns && byteOffset < expanded.size(); ++taken)
			{
				byteOffset = Core::NextCharacter(expanded, byteOffset);
			}

			return byteOffset;
		}

		void DrawFailure(const Core::OpenItem& item)
		{
			const std::string message = Core::FailureMessage(item);
			const ImVec2 size = ImGui::CalcTextSize(message.c_str());
			const ImVec2 available = ImGui::GetContentRegionAvail();

			ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + (available.x - size.x) * kHalf,
									   ImGui::GetCursorPosY() + (available.y - size.y) * kHalf));
			ImGui::TextDisabled("%s", message.c_str());
		}

		bool DrawWithoutDocumentAndReportRetry(const Core::OpenItem& item)
		{
			if (Core::IsLoading(item))
			{
				Core::LoadingState(Core::LoadingMessage(item).c_str());
				return false;
			}

			if (Core::CanRetryLoad(item))
			{
				return Core::EmptyState(Core::FailureMessage(item), kRetryLabel);
			}

			DrawFailure(item);
			return false;
		}

		struct GutterMetrics
		{
			float BreakpointWidth = 0.0f;
			float ChangeWidth = 0.0f;
			LineNumberColumn Numbers;
			float FoldWidth = 0.0f;
			float TotalWidth = 0.0f;
		};

		GutterMetrics MeasureGutter(std::size_t lineCount, float cellWidth, bool changeColumn)
		{
			GutterMetrics gutter;
			gutter.BreakpointWidth = kBreakpointColumnCells * cellWidth;
			gutter.ChangeWidth = changeColumn ? Core::Scaled(kChangeColumnWidth) : 0.0f;
			gutter.FoldWidth = kFoldColumnCells * cellWidth;
			gutter.Numbers = MeasureLineNumberColumn(lineCount, cellWidth);
			gutter.TotalWidth = gutter.BreakpointWidth + gutter.ChangeWidth + gutter.Numbers.Width() + gutter.FoldWidth;

			return gutter;
		}

		struct EditorMetrics
		{
			float FontSizeBase = 0.0f;
			float FontSize = 0.0f;
			float LineHeight = 0.0f;
			float TextOffsetY = 0.0f;
			float CellWidth = 0.0f;
			GutterMetrics Gutter;
		};

		float CentredTextOffset(float lineHeight, float textHeight)
		{
			return std::floor((lineHeight - textHeight) * kHalf);
		}

		float TextTopOf(const EditorMetrics& metrics, float rowTop)
		{
			return rowTop + metrics.TextOffsetY;
		}

		float TextBottomOf(const EditorMetrics& metrics, float rowTop)
		{
			return TextTopOf(metrics, rowTop) + metrics.FontSize;
		}

		// WARNING: The line height is taken from the font size in effect now, not from kCodeFontSize, so it
		// follows the zoom and the interface scale on the frame they change.
		EditorMetrics MeasureEditor(const Core::TextBuffer& buffer, bool changeColumn)
		{
			EditorMetrics metrics;

			metrics.FontSizeBase = ImGui::GetStyle().FontSizeBase;
			metrics.FontSize = ImGui::GetFontSize();
			metrics.LineHeight = std::round(kLineHeightFactor * metrics.FontSize);
			metrics.TextOffsetY = CentredTextOffset(metrics.LineHeight, metrics.FontSize);
			metrics.CellWidth = Core::MonospaceCellWidth();
			metrics.Gutter = MeasureGutter(buffer.GetLineCount(), metrics.CellWidth, changeColumn);

			return metrics;
		}

		WrapGeometry WrapGeometryOf(const GutterMetrics& gutter, float cellWidth, float widthWithoutScrollbar)
		{
			WrapGeometry geometry;
			geometry.ScrollbarWidth = ImGui::GetStyle().ScrollbarSize;
			geometry.WidthBesideScrollbar = widthWithoutScrollbar - geometry.ScrollbarWidth;
			geometry.FixedGutterWidth = gutter.BreakpointWidth + gutter.ChangeWidth + gutter.FoldWidth;
			geometry.CellWidth = cellWidth;
			geometry.LineNumberGap = gutter.Numbers.Gap;

			return geometry;
		}

		// WARNING: The surface has no padding, so its width without a scrollbar is the window's here, where it would begin.
		WrapGeometry WrapGeometryBeforeTheText(const EditorView& view)
		{
			constexpr std::size_t kNoLines = 0;

			ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont(), view.FontSize);
			const float cellWidth = Core::MonospaceCellWidth();
			const GutterMetrics gutter = MeasureGutter(kNoLines, cellWidth, view.ChangeDisplay.Gutter);
			const float widthWithoutScrollbar = ImGui::GetContentRegionAvail().x;
			ImGui::PopFont();

			return WrapGeometryOf(gutter, cellWidth, widthWithoutScrollbar);
		}

		float SurfaceWidthWithoutScrollbar()
		{
			const float availableNow = ImGui::GetContentRegionAvail().x;
			const bool scrollbarShown = ImGui::GetCurrentWindow()->ScrollbarY;

			return scrollbarShown ? availableNow + ImGui::GetStyle().ScrollbarSize : availableNow;
		}

		// WARNING: NoNavInputs is needed as much as the scrollbar flag. With ImGui's keyboard
		// navigation on and nothing navigable in this child, arrows, Page Up/Down and Home/End
		// also scroll the window and fight the caret-driven scrolling in the same frame.
		ImGuiWindowFlags EditorSurfaceFlags()
		{
			return ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNavInputs;
		}

		void RewrapIfGeometryChanged(Core::OpenItem& item, const Core::TextBuffer& buffer,
									 std::size_t columnsPerRow)
		{
			// WARNING: The line count must be compared as well as the width. A document can reach
			// here without passing through RecomputeLineCaches, and a map with more lines than the
			// buffer sends RowAt off its end.
			if (columnsPerRow == item.ColumnsPerWrappedRow &&
				item.Rows.GetLineCount() == buffer.GetLineCount())
			{
				return;
			}

			item.Rows.Rebuild(buffer, columnsPerRow, item.TabWidth);
			item.ColumnsPerWrappedRow = columnsPerRow;
			item.RowAimCaretOffset = Core::NoRowAim;
		}

		float ContentWidthOf(const Core::OpenItem& item, const EditorMetrics& metrics,
							 float available, bool wordWrap)
		{
			if (wordWrap)
			{
				return available;
			}

			const std::size_t columnsWithNewlineCell = item.WidestColumn + kNewlineCells;

			return metrics.Gutter.TotalWidth +
				   static_cast<float>(columnsWithNewlineCell) * metrics.CellWidth;
		}

		float DocumentHeightOf(const Core::OpenItem& item, const EditorMetrics& metrics,
							   const ScreenRows& screenRows)
		{
			return static_cast<float>(screenRows.TotalScreenRows(item.Rows.GetTotalRows())) *
				   metrics.LineHeight;
		}

		// WARNING: ImGui shows a new surface's scrollbar only from its second frame, so a tall document would be wrapped twice.
		VerticalScrollbar ScrollbarOfSurface(const Core::OpenItem& item, const EditorMetrics& metrics,
											 const ScreenRows& screenRows)
		{
			const bool scrollbarShown = ImGui::GetCurrentWindow()->ScrollbarY;
			const float contentHeightAsImGuiMeasuresIt = ImTrunc64(DocumentHeightOf(item, metrics, screenRows));
			const bool documentTallerThanView = contentHeightAsImGuiMeasuresIt > ImGui::GetContentRegionAvail().y;

			return scrollbarShown || documentTallerThanView ? VerticalScrollbar::Shown : VerticalScrollbar::Hidden;
		}

		// WARNING: Rows as wide as a frame without the coming scrollbar would show a horizontal one in the frame it arrives.
		float RowAreaWidth(float availableNow, VerticalScrollbar scrollbar)
		{
			const bool scrollbarStillToCome = scrollbar == VerticalScrollbar::Shown && !ImGui::GetCurrentWindow()->ScrollbarY;

			return scrollbarStillToCome ? availableNow - ImGui::GetStyle().ScrollbarSize : availableNow;
		}

		// WARNING: Deliberately not translated through ScreenRows. The count is handed to
		// MoveByRows, which works in document rows through CaretToVisual, so a page move
		// overshoots by the lens and peek rows it crosses; translating here would push screen-row
		// arithmetic into caret motion.
		std::size_t RowsPerPage(const EditorMetrics& metrics)
		{
			return static_cast<std::size_t>(
				std::max(1.0f, ImGui::GetWindowHeight() / metrics.LineHeight));
		}

		struct PointerState
		{
			bool PressedThisFrame = false;
			bool Dragging = false;
			bool RightPressedThisFrame = false;
			bool Hovered = false;
			bool Held = false;
		};

		// WARNING: ImGui::InvisibleButton would call ItemSize on the whole interactive area,
		// which is at least as big as the window. ContentSize would then grow to the window on
		// every file and show both scrollbars. ItemAdd plus ButtonBehavior give the same hit
		// testing and activation without ItemSize.
		PointerState AddInteractiveSurface(const ImVec2& origin, float contentWidth,
										   float documentHeight)
		{
			const ImVec2 extent(std::max(contentWidth, ImGui::GetWindowWidth()),
								std::max(documentHeight, ImGui::GetWindowHeight()));
			const ImRect bounds(origin, ImVec2(origin.x + extent.x, origin.y + extent.y));
			const ImGuiID id = ImGui::GetID("##EditorInput");

			// WARNING: NoNavFocus is needed as much as NoNav. ButtonBehavior takes navigation focus on
			// a click regardless of the item's flags, which makes the whole document the focused
			// item's rectangle, and ImGuiListClipper then adds that rectangle to the visible range:
			// every row of the file is drawn on every frame from the first click on.
			ImGui::ItemAdd(bounds, id, nullptr, ImGuiItemFlags_NoNav);
			ImGui::ButtonBehavior(bounds, id, nullptr, nullptr, ImGuiButtonFlags_NoNavFocus);

			PointerState pointer;

			pointer.PressedThisFrame = ImGui::IsItemActivated();

			// WARNING: A drag needs the mouse to have moved since the press. Without that, every
			// frame between a double or triple click's press and release would extend the
			// selection to the still mouse and collapse the word or line just selected.
			pointer.Dragging =
				ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

			pointer.RightPressedThisFrame =
				ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Right);

			pointer.Hovered = ImGui::IsItemHovered();
			pointer.Held = ImGui::IsItemActive();

			return pointer;
		}

		std::string LensLabelOf(const Lens& lens)
		{
			const bool singular = lens.Count == 1;

			return std::to_string(lens.Count) + (singular ? " reference" : " references");
		}

		std::string LensRowText(const Lens& lens, std::string_view waiting)
		{
			if (lens.CountKnown)
			{
				return LensLabelOf(lens);
			}

			if (lens.CountFailed)
			{
				return std::string{kLensCountUnavailable};
			}

			return std::string{waiting};
		}

		// WARNING: There is no ScrollX term here. textOriginX derives from the origin, which
		// already carries the horizontal scroll, so subtracting it again would slide every lens
		// away from its declaration when the view scrolls sideways.
		float LensLabelLeft(const Lens& lens, float textOriginX, const EditorMetrics& metrics)
		{
			return textOriginX + static_cast<float>(lens.Column) * metrics.CellWidth;
		}

		// WARNING: The hit-test and the drawing both push this, so a label is hit across the width it is
		// drawn at. A label measured in the code face it sits inside would be hit short of its end or past it.
		void PushLensFont(const EditorMetrics& metrics)
		{
			Core::PushInterfaceFont(metrics.FontSizeBase * kLensFontFactor);
		}

		bool IsOverLensLabel(const Lens* lens, float x, float textOriginX,
							 const EditorMetrics& metrics)
		{
			if (lens == nullptr || !lens->CountKnown)
			{
				return false;
			}

			const float left = LensLabelLeft(*lens, textOriginX, metrics);
			const std::string text = LensLabelOf(*lens);

			PushLensFont(metrics);
			const float width = ImGui::CalcTextSize(text.c_str()).x;
			ImGui::PopFont();

			return x >= left && x < left + width;
		}

		std::optional<ScreenRows::Row> RowUnderMouse(const ScreenRows& screenRows,
													 const EditorMetrics& metrics,
													 const ImVec2& origin)
		{
			const ImVec2 mouse = ImGui::GetMousePos();

			if (mouse.y < origin.y)
			{
				return std::nullopt;
			}

			return screenRows.At(static_cast<std::size_t>((mouse.y - origin.y) / metrics.LineHeight));
		}

		std::optional<std::size_t> HoveredLensRow(const PointerState& pointer,
												  const ScreenRows& screenRows, const LensModel* lensModel,
												  const EditorMetrics& metrics, const ImVec2& origin,
												  float textOriginX)
		{
			if (!pointer.Hovered || pointer.Dragging || lensModel == nullptr)
			{
				return std::nullopt;
			}

			const std::optional<ScreenRows::Row> hit = RowUnderMouse(screenRows, metrics, origin);

			if (!hit || hit->Kind != ScreenRows::RowKind::Lens ||
				!IsOverLensLabel(lensModel->LensAt(hit->DocumentRow), ImGui::GetMousePos().x,
								 textOriginX, metrics))
			{
				return std::nullopt;
			}

			return hit->DocumentRow;
		}

		float GutterRightEdge(const ImVec2& origin, float scrollX, const EditorMetrics& metrics)
		{
			return origin.x + scrollX + metrics.Gutter.TotalWidth;
		}

		void SetEditorCursor(const PointerState& pointer, bool overLink, const ScreenRows& screenRows,
							 const EditorMetrics& metrics, const ImVec2& origin, float scrollX)
		{
			if (overLink)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
				return;
			}

			if (pointer.Held && ImGui::IsMouseDown(ImGuiMouseButton_Left))
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
				return;
			}

			if (!pointer.Hovered)
			{
				return;
			}

			const float gutterRight = GutterRightEdge(origin, scrollX, metrics);
			const std::optional<ScreenRows::Row> hit = RowUnderMouse(screenRows, metrics, origin);

			if (ImGui::GetMousePos().x >= gutterRight && hit &&
				hit->Kind == ScreenRows::RowKind::Document)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
			}
		}

		void ApplyMouseToCaret(const PointerState& pointer, const Core::TextBuffer& buffer,
							   Core::OpenItem& item, const EditorMetrics& metrics,
							   const ImVec2& origin, float textOriginX, const ScreenRows& screenRows,
							   const LensModel* lensModel, EditorFrameResult& result)
		{
			if (!pointer.PressedThisFrame && !pointer.Dragging && !pointer.RightPressedThisFrame)
			{
				return;
			}

			const ImVec2 mouse = ImGui::GetMousePos();

			// WARNING: Both axes are clamped to zero before the cast. Converting a negative float
			// to std::size_t gives an enormous number, and as a column it wraps around inside
			// VisualToCaret and lands mid-row instead of at the row's beginning.
			const auto screenRow = static_cast<std::size_t>(
				std::max(0.0f, (mouse.y - origin.y) / metrics.LineHeight));

			const ScreenRows::Row hit = screenRows.At(screenRow);
			const bool onText = hit.Kind == ScreenRows::RowKind::Document;

			if (pointer.RightPressedThisFrame)
			{
				const float gutterRight = GutterRightEdge(origin, ImGui::GetScrollX(), metrics);

				if (!onText || mouse.x < gutterRight)
				{
					return;
				}
			}

			// WARNING: Only a press returns here. A drag across a lens must still extend to the
			// row under the pointer; returning for it as well dropped the extend and made the
			// selection stutter a row short while the pointer sat over a lens.
			if (!onText && pointer.PressedThisFrame)
			{
				const Lens* const lens = lensModel != nullptr && hit.Kind == ScreenRows::RowKind::Lens
											 ? lensModel->LensAt(hit.DocumentRow)
											 : nullptr;

				if (IsOverLensLabel(lens, mouse.x, textOriginX, metrics))
				{
					result.LensClicked = hit.DocumentRow;
				}

				return;
			}

			const std::size_t row = hit.DocumentRow;

			const float relativeX = std::max(0.0f, mouse.x - textOriginX);
			const auto column = static_cast<std::size_t>((relativeX + metrics.CellWidth * kHalf) /
														 metrics.CellWidth);

			const std::size_t offset =
				Core::VisualToCaret(buffer, item.Rows, Core::VisualPosition{.Row = row, .Column = column},
									item.TabWidth);

			if (pointer.RightPressedThisFrame)
			{
				result.ContextMenuOffset = offset;

				const auto [selectionFrom, selectionTo] = Core::SelectionRange(item.Caret);
				const bool insideSelection =
					selectionFrom != selectionTo && offset >= selectionFrom && offset <= selectionTo;

				if (!insideSelection)
				{
					item.Caret = Core::PlaceAt(buffer, item.Caret, offset,
											   Core::SelectionChange::Collapse, item.TabWidth);
				}

				return;
			}

			// WARNING: The word is taken from the character under the pointer, not from the caret
			// position the click rounds to. Rounded, the right half of a word's last character
			// lands on whatever follows the word, and that is what got selected.
			const auto cellUnderPointer = static_cast<std::size_t>(relativeX / metrics.CellWidth);
			const std::size_t characterUnderPointer = Core::VisualToCaret(
				buffer, item.Rows, Core::VisualPosition{.Row = row, .Column = cellUnderPointer}, item.TabWidth);

			if (!pointer.PressedThisFrame)
			{
				if (item.DragUnit == Core::SelectionUnit::Character)
				{
					item.Caret = Core::PlaceAt(buffer, item.Caret, offset, Core::SelectionChange::Extend,
											   item.TabWidth);
				}
				else
				{
					item.Caret = Core::ExtendByUnit(buffer, item.DragOrigin, characterUnderPointer,
													item.DragUnit);
				}

				return;
			}

			const int clicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
			item.DragUnit = Core::SelectionUnit::Character;

			if (clicks >= kTripleClick)
			{
				item.Caret = Core::SelectLineAt(buffer, item.Rows.RowAt(row).Line);
				item.DragUnit = Core::SelectionUnit::Line;
				item.DragOrigin = Core::SelectionRange(item.Caret);
			}
			else if (clicks == kDoubleClick)
			{
				item.Caret = Core::SelectWordAt(buffer, characterUnderPointer);
				item.DragUnit = Core::SelectionUnit::Word;
				item.DragOrigin = Core::SelectionRange(item.Caret);
			}
			else
			{
				const Core::SelectionChange selectionChange = ImGui::GetIO().KeyShift
																  ? Core::SelectionChange::Extend
																  : Core::SelectionChange::Collapse;

				item.Caret = Core::PlaceAt(buffer, item.Caret, offset, selectionChange, item.TabWidth);

				if (ImGui::GetIO().KeyCtrl)
				{
					result.DefinitionRequested = true;
				}
			}
		}

		bool IsFullyRoutable(const CommandContext& context)
		{
			return context.Router != nullptr && context.Commands != nullptr &&
				   context.Keys != nullptr;
		}

		void RouteMouseWheel(const CommandContext& context)
		{
			const bool routable = IsFullyRoutable(context);

			if (!ImGui::IsWindowHovered() || !routable)
			{
				return;
			}

			const Core::MouseScrolledEvent scrolled{ImGui::GetIO().MouseWheelH,
													ImGui::GetIO().MouseWheel};

			context.Router->OnMouseScrolled(*context.Commands, *context.Keys,
											Core::ShortcutScope::HoveredEditor, scrolled);
		}

		bool ShiftOf(const Core::KeyStroke& stroke)
		{
			return Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Shift);
		}

		// WARNING: On Windows AltGr reports as Ctrl+Alt, so a Polish letter typed with AltGr
		// would read as Ctrl+<letter>. Excluding Alt keeps every shortcut here from firing on it.
		bool ControlOf(const Core::KeyStroke& stroke)
		{
			return Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Control) &&
				   !Core::HasModifier(stroke.Modifiers, Core::KeyModifier::Alt);
		}

		std::size_t RowOfCaret(const Core::TextBuffer& buffer, const Core::OpenItem& item)
		{
			return Core::CaretToVisual(buffer, item.Rows, item.Caret.Offset, item.TabWidth)
				.Row;
		}

		void PlaceOnRow(const Core::TextBuffer& buffer, Core::OpenItem& item, std::size_t targetRow,
						std::size_t column, ColumnAim aim, bool shift)
		{
			const std::size_t offset = Core::VisualToCaret(
				buffer, item.Rows, Core::VisualPosition{.Row = targetRow, .Column = column},
				item.TabWidth);

			item.Caret.Offset = offset;

			if (!shift)
			{
				item.Caret.Anchor = offset;
			}

			if (aim == ColumnAim::KeepAiming)
			{
				item.RowAimColumn = column;
				item.RowAimCaretOffset = offset;
			}
			else
			{
				item.RowAimCaretOffset = Core::NoRowAim;
			}
		}

		std::size_t RowsAbove(std::size_t row, std::size_t count)
		{
			return row >= count ? row - count : 0;
		}

		void MoveByRows(const Core::TextBuffer& buffer, Core::OpenItem& item, RowDirection direction,
						std::size_t rows, bool shift)
		{
			const Core::VisualPosition here =
				Core::CaretToVisual(buffer, item.Rows, item.Caret.Offset, item.TabWidth);

			const bool aimIsCurrent = item.RowAimCaretOffset == item.Caret.Offset;
			const std::size_t aimColumn = aimIsCurrent ? item.RowAimColumn : here.Column;

			const std::size_t targetRow = direction == RowDirection::Down
											  ? here.Row + rows
											  : RowsAbove(here.Row, rows);

			PlaceOnRow(buffer, item, targetRow, aimColumn, ColumnAim::KeepAiming, shift);
		}

		void MoveToRowStart(const Core::TextBuffer& buffer, Core::OpenItem& item, bool shift)
		{
			PlaceOnRow(buffer, item, RowOfCaret(buffer, item), kRowStartColumn,
					   ColumnAim::StopAiming, shift);
		}

		// WARNING: On a wrapped row this lands one column short of the right edge: the offset
		// at the edge is also the next row's column zero, and the rule assigns it to the next
		// row. Reaching the edge needs a caret affinity bit. The column is halved from the
		// maximum because VisualToCaret adds the row's start column before clamping.
		void MoveToRowEnd(const Core::TextBuffer& buffer, Core::OpenItem& item, bool shift)
		{
			PlaceOnRow(buffer, item, RowOfCaret(buffer, item), kColumnBeyondAnyRow,
					   ColumnAim::StopAiming, shift);
		}

		void ApplyMotionKeys(const Core::FrameInput& input, const Core::TextBuffer& buffer,
							 Core::OpenItem& item, std::size_t pageRows)
		{
			struct Binding
			{
				Core::KeyCode Key;
				Core::Motion Plain;
				Core::Motion WithControl;
			};

			const Binding bindings[] = {
				{Core::KeyCode::Left, Core::Motion::Left, Core::Motion::WordLeft},
				{Core::KeyCode::Right, Core::Motion::Right, Core::Motion::WordRight},
			};

			for (const Core::KeyStroke& stroke : input.GetStrokes())
			{
				const bool shift = ShiftOf(stroke);
				const bool control = ControlOf(stroke);
				const Core::SelectionChange selectionChange =
					shift ? Core::SelectionChange::Extend : Core::SelectionChange::Collapse;

				for (const Binding& binding : bindings)
				{
					if (stroke.Key != binding.Key)
					{
						continue;
					}

					item.Caret = Core::Move(buffer, item.Caret,
											control ? binding.WithControl : binding.Plain,
											selectionChange, item.TabWidth, pageRows);
				}

				if (stroke.Key == Core::KeyCode::Up)
				{
					MoveByRows(buffer, item, RowDirection::Up, kOneRow, shift);
				}

				if (stroke.Key == Core::KeyCode::Down)
				{
					MoveByRows(buffer, item, RowDirection::Down, kOneRow, shift);
				}

				if (stroke.Key == Core::KeyCode::PageUp)
				{
					MoveByRows(buffer, item, RowDirection::Up, pageRows, shift);
				}

				if (stroke.Key == Core::KeyCode::PageDown)
				{
					MoveByRows(buffer, item, RowDirection::Down, pageRows, shift);
				}

				if (stroke.Key == Core::KeyCode::Home)
				{
					if (control)
					{
						item.Caret = Core::Move(buffer, item.Caret, Core::Motion::DocumentStart,
												selectionChange, item.TabWidth, pageRows);
					}
					else
					{
						MoveToRowStart(buffer, item, shift);
					}
				}

				if (stroke.Key == Core::KeyCode::End)
				{
					if (control)
					{
						item.Caret = Core::Move(buffer, item.Caret, Core::Motion::DocumentEnd,
												selectionChange, item.TabWidth, pageRows);
					}
					else
					{
						MoveToRowEnd(buffer, item, shift);
					}
				}
			}
		}

		struct EditSinks
		{
			LensModel* Lens = nullptr;
			std::vector<Declaration>* Declarations = nullptr;
		};

		struct EditMark
		{
			std::uint64_t ChangeSequence = 0;
			std::size_t LineCount = 0;
		};

		EditMark MarkBeforeEdit(const Core::TextBuffer& buffer)
		{
			return EditMark{.ChangeSequence = buffer.GetChangeSequence(), .LineCount = buffer.GetLineCount()};
		}

		// The lines to shift come from the buffer's own record of the edit, not from the caret: a
		// newline typed at the end of a declaration leaves the declaration, and its lens, in place,
		// and an undo or redo can touch lines the caret never visited.
		void NotifyAfterEdit(const EditSinks& sinks, const Core::TextBuffer& buffer,
							 const Core::OpenItem& item, const EditMark& before, bool changed)
		{
			if (!changed || (sinks.Lens == nullptr && sinks.Declarations == nullptr))
			{
				return;
			}

			const std::optional<std::span<const Core::TextChange>> changes = buffer.ChangesSince(before.ChangeSequence);
			const bool knowsTheEdit = changes.has_value() && !changes->empty();
			const std::size_t firstMovedLine = knowsTheEdit ? Core::FirstMovedLine(buffer, *changes) : kFirstLine;

			const std::ptrdiff_t lineCountChange =
				static_cast<std::ptrdiff_t>(buffer.GetLineCount()) -
				static_cast<std::ptrdiff_t>(before.LineCount);

			if (sinks.Lens != nullptr)
			{
				sinks.Lens->OnLinesChanged(firstMovedLine, lineCountChange, item.Rows);
			}

			if (sinks.Declarations != nullptr)
			{
				MoveDeclarations(*sinks.Declarations, firstMovedLine, lineCountChange);
			}
		}

		bool IsInsertableCodepoint(unsigned int codepoint)
		{
			const bool isControlCharacter =
				codepoint < kFirstPrintableCodepoint && codepoint != '\t';

			return !isControlCharacter && codepoint != kDeleteCodepoint;
		}

		bool InsertTypedCharacters(const Core::FrameInput& input, Core::OpenItem& item,
								   const EditSinks& sinks)
		{
			bool edited = false;

			const Core::TextBuffer& buffer = item.Document->GetBuffer();

			for (const unsigned int codepoint : input.GetCharacters())
			{
				if (!IsInsertableCodepoint(codepoint))
				{
					continue;
				}

				char utf8[kUtf8BufferSize];
				const int length = ImTextCharToUtf8(utf8, codepoint);

				if (length > 0)
				{
					const EditMark before = MarkBeforeEdit(buffer);
					const bool changed = Core::InsertText(
						item, std::string_view(utf8, static_cast<std::size_t>(length)));

					edited |= changed;
					NotifyAfterEdit(sinks, buffer, item, before, changed);
				}
			}

			return edited;
		}

		void ApplyEditKeys(const Core::FrameInput& input, const Core::TextBuffer& buffer,
						   Core::OpenItem& item, Core::IndentStyle indentStyle,
						   const EditSinks& sinks, EditorFrameResult& result)
		{
			for (const Core::KeyStroke& stroke : input.GetStrokes())
			{
				const bool shift = ShiftOf(stroke);
				const bool control = ControlOf(stroke);

				if (stroke.Key == Core::KeyCode::Enter || stroke.Key == Core::KeyCode::KeypadEnter)
				{
					const EditMark before = MarkBeforeEdit(buffer);
					const bool changed = Core::InsertNewline(item);
					result.Edited |= changed;
					NotifyAfterEdit(sinks, buffer, item, before, changed);
				}

				if (stroke.Key == Core::KeyCode::Backspace)
				{
					const EditMark before = MarkBeforeEdit(buffer);
					const bool changed =
						control ? Core::DeleteWordBackward(item) : Core::DeleteBackward(item);
					result.Edited |= changed;
					NotifyAfterEdit(sinks, buffer, item, before, changed);
				}

				if (stroke.Key == Core::KeyCode::Delete)
				{
					const EditMark before = MarkBeforeEdit(buffer);
					const bool changed =
						control ? Core::DeleteWordForward(item) : Core::DeleteForward(item);
					result.Edited |= changed;
					NotifyAfterEdit(sinks, buffer, item, before, changed);
				}

				// WARNING: Ctrl+Tab must not indent. NoNavInputs does not reach it because ImGui
				// routes the window switcher globally, so one keystroke would both open the
				// switcher and edit the document behind it.
				if (!control && stroke.Key == Core::KeyCode::Tab)
				{
					const EditMark before = MarkBeforeEdit(buffer);
					const bool changed =
						shift ? Core::Outdent(item, indentStyle) : Core::Indent(item, indentStyle);
					result.Edited |= changed;
					NotifyAfterEdit(sinks, buffer, item, before, changed);
				}
			}
		}

		void ApplyKeyboard(const Core::FrameInput& input, const Core::TextBuffer& buffer,
						   Core::OpenItem& item, Core::IndentStyle indentStyle,
						   const EditSinks& sinks, std::size_t pageRows,
						   std::size_t caretAtFrameStart, EditorFrameResult& result)
		{
			ApplyMotionKeys(input, buffer, item, pageRows);

			if (item.Caret.Offset != caretAtFrameStart)
			{
				item.Document->GetBuffer().BreakUndoGroup();
			}

			result.Edited |= InsertTypedCharacters(input, item, sinks);

			ApplyEditKeys(input, buffer, item, indentStyle, sinks, result);
		}

		bool IsHistoryAction(EditorAction action)
		{
			return action == EditorAction::Undo || action == EditorAction::Redo;
		}

		void ApplyPendingActions(std::vector<PendingEditorAction>& pending, Core::OpenItem& item,
								 const EditSinks& sinks, HistoryHold& history, bool repeatHeld,
								 EditorFrameResult& result)
		{
			if (pending.empty() || !item.Document)
			{
				return;
			}

			// WARNING: Swapped out before running, so an action that queues another lands in the
			// next frame instead of growing the vector under the loop.
			std::vector<PendingEditorAction> actionsThisFrame;
			actionsThisFrame.swap(pending);

			const Core::TextBuffer& buffer = item.Document->GetBuffer();

			for (const PendingEditorAction& queued : actionsThisFrame)
			{
				const EditorAction action = queued.Action;
				const EditMark before = MarkBeforeEdit(buffer);

				if (IsHistoryAction(action))
				{
					const double now = ImGui::GetTime();

					if (!HistoryStepIsDue(history, repeatHeld, now))
					{
						continue;
					}

					history = TakeHistoryStep(history, repeatHeld, now);
				}

				std::optional<std::string> clipboard;

				// WARNING: ClipboardHasText is asked first so a clipboard holding an image is
				// never read; GLFW would log the failed read as an error.
				if (action == EditorAction::Paste && Core::ClipboardHasText())
				{
					if (const char* text = ImGui::GetClipboardText(); text != nullptr)
					{
						clipboard = text;
					}
				}

				const EditorActionOutcome outcome = ApplyEditorAction(action, item, clipboard);

				if (outcome.ToClipboard)
				{
					ImGui::SetClipboardText(outcome.ToClipboard->c_str());

					if (queued.FromMenu)
					{
						result.CopiedFromMenu = *outcome.ToClipboard;
					}
				}

				result.Edited |= outcome.Changed;
				NotifyAfterEdit(sinks, buffer, item, before, outcome.Changed);
			}
		}

		// WARNING: scrollY is where the view will be, which is not always ImGui::GetScrollY():
		// SetScrollY only records a target and the getter reports the old offset until the next
		// Begin. A caller that corrected the scroll earlier in the frame must pass its own figure.
		void ScrollToCaret(const Core::TextBuffer& buffer, const Core::OpenItem& item,
						   const EditorMetrics& metrics, bool wordWrap, const ScreenRows& screenRows,
						   float scrollY)
		{
			const Core::Position caretPosition = buffer.ToPosition(item.Caret.Offset);

			const std::size_t caretDocumentRow =
				Core::CaretToVisual(buffer, item.Rows, item.Caret.Offset, item.TabWidth).Row;
			const float caretY =
				static_cast<float>(screenRows.ScreenRowOf(caretDocumentRow)) * metrics.LineHeight;

			if (caretY < scrollY)
			{
				ImGui::SetScrollY(caretY);
			}
			else if (caretY + metrics.LineHeight > scrollY + ImGui::GetWindowHeight())
			{
				ImGui::SetScrollY(caretY + metrics.LineHeight - ImGui::GetWindowHeight());
			}

			if (wordWrap)
			{
				return;
			}

			// WARNING: caretX is in content coordinates, but the gutter is painted over the
			// window's left edge instead of scrolling, so the leftmost visible text sits at
			// scrollX + gutterWidth and scrolling to the left edge means caretX - gutterWidth.
			const std::size_t caretColumn = Core::ColumnOfByte(
				buffer.LineText(caretPosition.Line), caretPosition.Column, item.TabWidth);
			const float caretX = metrics.Gutter.TotalWidth +
								 static_cast<float>(caretColumn) * metrics.CellWidth;
			const float caretBesideGutter = caretX - metrics.Gutter.TotalWidth;

			if (caretBesideGutter < ImGui::GetScrollX())
			{
				ImGui::SetScrollX(caretBesideGutter);
			}
			else if (caretX + metrics.CellWidth > ImGui::GetScrollX() + ImGui::GetWindowWidth())
			{
				ImGui::SetScrollX(caretX + metrics.CellWidth - ImGui::GetWindowWidth());
			}
		}

		// Under line L is before the first row of line L+1, or past the last row for the last line. A line past the end,
		// left by a document that shrank before its peek was closed, is placed as the last one is.
		PeekPlacement PlacePeekUnder(const Core::RowMap& rows, std::size_t line)
		{
			const std::size_t totalRows = rows.GetTotalRows();
			const std::size_t before = line + 1 < rows.GetLineCount() ? rows.FirstRowOf(line + 1) : totalRows;

			return PeekPlacement{.BeforeDocumentRow = std::min(before, totalRows), .Rows = kLinePeekRows};
		}

		// Scrolls just far enough to show the whole peek and the line it sits under, or puts that line at the top when
		// the two do not fit together. scrollY is where the view will be, as for ScrollToCaret.
		void ScrollPeekIntoView(const Core::OpenItem& item, const EditorMetrics& metrics, const ScreenRows& screenRows,
								const PeekPlacement& placement, std::size_t line, float scrollY)
		{
			const std::optional<std::size_t> peekTop = screenRows.PeekTop();

			if (!peekTop || item.Rows.GetLineCount() == 0)
			{
				return;
			}

			const std::size_t shownLine = std::min(line, item.Rows.GetLineCount() - 1);
			const float lineTop =
				static_cast<float>(screenRows.ScreenRowOf(item.Rows.FirstRowOf(shownLine))) * metrics.LineHeight;
			const float peekBottom = static_cast<float>(*peekTop + placement.Rows) * metrics.LineHeight;
			const float visibleHeight = ImGui::GetCurrentWindow()->InnerRect.GetHeight();

			if (peekBottom > scrollY + visibleHeight)
			{
				const bool fitsWithItsLine = peekBottom - lineTop <= visibleHeight;

				ImGui::SetScrollY(fitsWithItsLine ? peekBottom - visibleHeight : lineTop);
			}
			else if (lineTop < scrollY)
			{
				ImGui::SetScrollY(lineTop);
			}
		}

		struct RowCanvas
		{
			ImDrawList* Draw = nullptr;
			ImVec2 Origin;
			float EditorLeft = 0.0f;
			float ScrollX = 0.0f;
			float TextOriginX = 0.0f;
			float ContentWidth = 0.0f;
			float WindowWidth = 0.0f;
			EditorMetrics Metrics;
			EditorPalette Palette;
			SyntaxPalette SyntaxColours;
		};

		struct RowSlice
		{
			std::size_t Index = 0;
			std::size_t Line = 0;
			std::size_t SubRow = 0;
			float Y = 0.0f;
			std::size_t FromColumn = 0;
			std::size_t ToColumn = 0;
			bool LastRowOfLine = false;
			std::size_t FromByte = 0;
			std::size_t ToByte = 0;
		};

		struct VisibleLineCache
		{
			std::string Text;
			std::string Expanded;
			std::size_t Line = 0;
			bool Valid = false;

			bool SelectionOnLine = false;
			std::size_t SelectionFromColumn = 0;
			std::size_t SelectionToColumn = 0;

			LineMatches Matches;

			LineMatches Occurrences;

			LineMatches Brackets;
			bool BracketsUnmatched = false;

			std::size_t CarriedByte = 0;
			std::size_t CarriedRow = 0;
			bool HaveCarried = false;
		};

		struct LineCacheInput
		{
			const Core::TextBuffer& Buffer;
			const std::vector<Core::FindMatch>& Matches;
			const std::vector<Core::FindMatch>& Occurrences;

			Core::OffsetRange Selection;
			std::size_t CurrentMatch = 0;
			std::size_t SelectedOccurrence = 0;
			std::size_t TabWidth = Core::DefaultTabWidth;

			std::optional<BracketPair> Brackets;
		};

		void CacheLine(VisibleLineCache& cache, const LineCacheInput& input, std::size_t line)
		{
			if (cache.Valid && cache.Line == line)
			{
				return;
			}

			const auto [selectionFrom, selectionTo] = input.Selection;
			const std::size_t tabWidth = input.TabWidth;

			cache.Text = input.Buffer.LineText(line);
			cache.Expanded = Core::ExpandTabs(cache.Text, tabWidth);
			cache.Line = line;
			cache.Valid = true;
			cache.HaveCarried = false;

			const std::size_t start = input.Buffer.LineStart(line);

			LineMatchInput matchInput{input.Matches};

			matchInput.LineText = cache.Text;
			matchInput.LineStart = start;
			matchInput.CurrentMatch = input.CurrentMatch;
			matchInput.TabWidth = tabWidth;

			cache.Matches = MatchColumnsOnLine(matchInput);

			LineMatchInput occurrenceInput{input.Occurrences};

			occurrenceInput.LineText = cache.Text;
			occurrenceInput.LineStart = start;
			occurrenceInput.CurrentMatch = input.SelectedOccurrence;
			occurrenceInput.TabWidth = tabWidth;

			cache.Occurrences = MatchColumnsOnLine(occurrenceInput);

			cache.Brackets.Columns.clear();
			cache.Brackets.Current = 0;
			cache.BracketsUnmatched = input.Brackets && !input.Brackets->Matched;

			if (input.Brackets)
			{
				const std::size_t end = start + cache.Text.size();

				for (const std::size_t offset : {input.Brackets->Open, input.Brackets->Close})
				{
					if (offset < start || offset >= end)
					{
						continue;
					}

					const std::size_t column =
						Core::ColumnOfByte(cache.Text, offset - start, tabWidth);

					cache.Brackets.Columns.push_back(MatchColumns{column, column + 1});
				}

				// WARNING: An unmatched bracket reports the same offset twice; drawing its box
				// twice over itself would show a darker square than the palette asked for.
				const bool boxesCoincide =
					cache.Brackets.Columns.size() == 2 &&
					cache.Brackets.Columns[0].FromColumn == cache.Brackets.Columns[1].FromColumn;

				if (boxesCoincide)
				{
					cache.Brackets.Columns.pop_back();
				}
			}

			cache.SelectionFromColumn = 0;
			cache.SelectionToColumn = 0;
			cache.SelectionOnLine = selectionFrom != selectionTo && selectionTo > start &&
									selectionFrom <= start + cache.Text.size();

			if (!cache.SelectionOnLine)
			{
				return;
			}

			const std::size_t fromByte = selectionFrom > start ? selectionFrom - start : 0;
			const std::size_t toByte = std::min(selectionTo - start, cache.Text.size() + 1);

			const bool selectionCoversNewline = toByte > cache.Text.size();
			const std::size_t newlineHighlightCells = selectionCoversNewline ? kNewlineCells : 0;

			cache.SelectionFromColumn = Core::ColumnOfByte(cache.Text, fromByte, tabWidth);
			cache.SelectionToColumn =
				Core::ColumnOfByte(cache.Text, std::min(toByte, cache.Text.size()), tabWidth) +
				newlineHighlightCells;
		}

		std::size_t FirstColumnOfRow(const Core::RowMap& rows, Core::RowRef rowRef)
		{
			const std::vector<std::size_t>& breaks = rows.BreaksOf(rowRef.Line);

			return rowRef.SubRow == 0 || rowRef.SubRow > breaks.size() ? 0
																	   : breaks[rowRef.SubRow - 1];
		}

		// WARNING: The breaks are guarded instead of indexed straight. RowsOf(line) equals
		// BreaksOf(line).size() + 1 today, but RowMap::SetRowsOf forces a count without touching
		// the breaks, so a line set to zero rows would read past the end. Falling back to the
		// line's first column draws a wrong slice instead of reading foreign memory.
		RowSlice SliceRow(VisibleLineCache& cache, const Core::RowMap& rows, Core::RowRef rowRef,
						  std::size_t rowIndex, float y)
		{
			RowSlice slice;

			slice.Index = rowIndex;
			slice.Line = rowRef.Line;
			slice.SubRow = rowRef.SubRow;
			slice.Y = y;

			const std::vector<std::size_t>& breaks = rows.BreaksOf(rowRef.Line);

			slice.FromColumn = FirstColumnOfRow(rows, rowRef);
			slice.LastRowOfLine = rowRef.SubRow >= breaks.size();
			slice.ToColumn = slice.LastRowOfLine ? std::numeric_limits<std::size_t>::max()
												 : breaks[rowRef.SubRow];

			// WARNING: Columns are converted to bytes instead of indexing the expanded string
			// with them: one column is one cell, not one byte, and a line holding anything
			// outside ASCII would be cut mid-character.
			slice.FromByte = cache.HaveCarried && cache.CarriedRow == rowIndex
								 ? cache.CarriedByte
								 : AdvanceColumns(cache.Expanded, 0, slice.FromColumn);
			slice.ToByte = slice.LastRowOfLine
							   ? cache.Expanded.size()
							   : AdvanceColumns(cache.Expanded, slice.FromByte,
												slice.ToColumn - slice.FromColumn);

			cache.CarriedByte = slice.ToByte;
			cache.CarriedRow = rowIndex + 1;
			cache.HaveCarried = true;

			return slice;
		}

		struct ColumnSpan
		{
			std::size_t FromColumn = 0;
			std::size_t ToColumn = 0;

			bool IsEmpty() const
			{
				return ToColumn <= FromColumn;
			}
		};

		ColumnSpan ClipToRow(const RowSlice& slice, std::size_t fromColumn, std::size_t toColumn)
		{
			return ColumnSpan{.FromColumn = std::clamp(fromColumn, slice.FromColumn, slice.ToColumn),
							  .ToColumn = std::clamp(toColumn, slice.FromColumn, slice.ToColumn)};
		}

		float ColumnX(const RowCanvas& canvas, const RowSlice& slice, std::size_t column)
		{
			return canvas.TextOriginX +
				   static_cast<float>(column - slice.FromColumn) * canvas.Metrics.CellWidth;
		}

		// WARNING: The fill starts at the editor's left edge, across the padding of the window that holds the
		// surface, and the surface's clip rectangle stops at the surface. It is drawn under that clip widened to the
		// edge, keeping the clip's top and bottom, so it never reaches over the breadcrumb or the scrollbar below.
		void DrawCurrentLineHighlight(const RowCanvas& canvas, const RowSlice& slice)
		{
			constexpr bool kIntersectWithTheSurfaceClip = false;

			const ImVec2 clipMin = canvas.Draw->GetClipRectMin();
			const ImVec2 clipMax = canvas.Draw->GetClipRectMax();

			canvas.Draw->PushClipRect(ImVec2(std::min(canvas.EditorLeft, clipMin.x), clipMin.y), clipMax,
									  kIntersectWithTheSurfaceClip);
			canvas.Draw->AddRectFilled(
				ImVec2(canvas.EditorLeft, slice.Y),
				ImVec2(canvas.Origin.x + std::max(canvas.ContentWidth, canvas.WindowWidth),
					   slice.Y + canvas.Metrics.LineHeight),
				canvas.Palette.CurrentLine);
			canvas.Draw->PopClipRect();
		}

		void DrawIndentGuides(const RowCanvas& canvas, const RowSlice& slice, std::size_t depth,
							  std::size_t tabWidth)
		{
			for (std::size_t column = 0; column < depth; column += tabWidth)
			{
				if (column < slice.FromColumn)
				{
					continue;
				}

				const float guideX = ColumnX(canvas, slice, column);

				canvas.Draw->AddLine(ImVec2(guideX, slice.Y),
									 ImVec2(guideX, slice.Y + canvas.Metrics.LineHeight),
									 canvas.Palette.Guide, kHairlineWidth);
			}
		}

		void FillRowColumns(const RowCanvas& canvas, const RowSlice& slice, const MatchColumns& match,
							ImU32 color)
		{
			const ColumnSpan clipped = ClipToRow(slice, match.FromColumn, match.ToColumn);

			if (clipped.IsEmpty())
			{
				return;
			}

			const float fromX = ColumnX(canvas, slice, clipped.FromColumn);
			const float toX = ColumnX(canvas, slice, clipped.ToColumn);

			canvas.Draw->AddRectFilled(ImVec2(fromX, slice.Y),
									   ImVec2(toX, slice.Y + canvas.Metrics.LineHeight), color);
		}

		void DrawRowMatches(const RowCanvas& canvas, const RowSlice& slice,
							const VisibleLineCache& cache)
		{
			for (std::size_t index = 0; index < cache.Matches.Columns.size(); ++index)
			{
				const ImU32 color = index == cache.Matches.Current
										? canvas.Palette.FindCurrentMatch
										: canvas.Palette.FindMatch;

				FillRowColumns(canvas, slice, cache.Matches.Columns[index], color);
			}
		}

		// The selected occurrence is left out: the selection already covers it.
		void DrawRowOccurrences(const RowCanvas& canvas, const RowSlice& slice,
								const VisibleLineCache& cache)
		{
			for (std::size_t index = 0; index < cache.Occurrences.Columns.size(); ++index)
			{
				if (index != cache.Occurrences.Current)
				{
					FillRowColumns(canvas, slice, cache.Occurrences.Columns[index],
								   canvas.Palette.SelectionOccurrence);
				}
			}
		}

		void DrawRowCurrentMatchOutline(const RowCanvas& canvas, const RowSlice& slice,
										const VisibleLineCache& cache)
		{
			if (cache.Matches.Current >= cache.Matches.Columns.size())
			{
				return;
			}

			const MatchColumns& match = cache.Matches.Columns[cache.Matches.Current];
			const ColumnSpan clipped = ClipToRow(slice, match.FromColumn, match.ToColumn);

			if (clipped.IsEmpty())
			{
				return;
			}

			const float fromX = ColumnX(canvas, slice, clipped.FromColumn);
			const float toX = ColumnX(canvas, slice, clipped.ToColumn);

			canvas.Draw->AddRect(ImVec2(fromX, slice.Y),
								 ImVec2(toX, slice.Y + canvas.Metrics.LineHeight),
								 Core::WithOpaqueAlpha(canvas.Palette.FindCurrentMatch));
		}

		void DrawRowBrackets(const RowCanvas& canvas, const RowSlice& slice,
							 const VisibleLineCache& cache)
		{
			const ImU32 color = cache.BracketsUnmatched ? canvas.Palette.UnmatchedBracket
														: canvas.Palette.MatchingBracket;

			for (const MatchColumns& bracket : cache.Brackets.Columns)
			{
				const ColumnSpan clipped = ClipToRow(slice, bracket.FromColumn, bracket.ToColumn);

				if (clipped.IsEmpty())
				{
					continue;
				}

				const float fromX = ColumnX(canvas, slice, clipped.FromColumn);
				const float toX = ColumnX(canvas, slice, clipped.ToColumn);

				canvas.Draw->AddRectFilled(ImVec2(fromX, slice.Y),
										   ImVec2(toX, slice.Y + canvas.Metrics.LineHeight),
										   color);
			}
		}

		void DrawRowSelection(const RowCanvas& canvas, const RowSlice& slice,
							  const VisibleLineCache& cache)
		{
			const ColumnSpan clipped =
				ClipToRow(slice, cache.SelectionFromColumn, cache.SelectionToColumn);

			if (clipped.IsEmpty())
			{
				return;
			}

			const float fromX = ColumnX(canvas, slice, clipped.FromColumn);
			const float toX = ColumnX(canvas, slice, clipped.ToColumn);

			canvas.Draw->AddRectFilled(ImVec2(fromX, slice.Y),
									   ImVec2(toX, slice.Y + canvas.Metrics.LineHeight),
									   canvas.Palette.Selection);
		}

		struct TextRunPainter
		{
			const RowCanvas& Canvas;
			const RowSlice& Slice;
			const char* Base;
			std::size_t TabWidth;
			std::size_t PaintedUpToByte;
			std::size_t Column;

			void PaintUpTo(std::size_t toByte, ImU32 colour)
			{
				if (toByte <= PaintedUpToByte)
				{
					return;
				}

				const std::string_view run(Base + PaintedUpToByte, toByte - PaintedUpToByte);
				const float runX = ColumnX(Canvas, Slice, Column);

				Canvas.Draw->AddText(ImVec2(runX, TextTopOf(Canvas.Metrics, Slice.Y)), colour, run.data(),
									 run.data() + run.size());

				// WARNING: TabWidth is inert here. The run slices the already tab-expanded text,
				// so ColumnCount sees no tab to size.
				Column += Core::ColumnCount(run, TabWidth);
				PaintedUpToByte = toByte;
			}
		};

		void DrawRowText(const RowCanvas& canvas, const RowSlice& slice,
						 const VisibleLineCache& cache, const std::vector<Token>& tokens,
						 const std::vector<SemanticSpan>& semantic, std::size_t tabWidth)
		{
			if (slice.FromByte >= slice.ToByte)
			{
				return;
			}

			const char* const base = cache.Expanded.data();

			if (tokens.empty())
			{
				canvas.Draw->AddText(ImVec2(canvas.TextOriginX, TextTopOf(canvas.Metrics, slice.Y)),
									 canvas.Palette.Text, base + slice.FromByte, base + slice.ToByte);
				return;
			}

			TextRunPainter painter{.Canvas = canvas,
								   .Slice = slice,
								   .Base = base,
								   .TabWidth = tabWidth,
								   .PaintedUpToByte = slice.FromByte,
								   .Column = slice.FromColumn};

			for (const Token& token : tokens)
			{
				if (token.To <= slice.FromByte)
				{
					continue;
				}

				if (token.From >= slice.ToByte)
				{
					break;
				}

				painter.PaintUpTo(std::max(token.From, slice.FromByte), canvas.Palette.Text);
				painter.PaintUpTo(std::min(token.To, slice.ToByte),
								  canvas.SyntaxColours.Of(ColourOf(token, semantic)));
			}

			painter.PaintUpTo(slice.ToByte, canvas.Palette.Text);
		}

		void DrawGutterFill(const RowCanvas& canvas, float y)
		{
			const float gutterX = canvas.Origin.x + canvas.ScrollX;

			canvas.Draw->AddRectFilled(
				ImVec2(gutterX, y),
				ImVec2(gutterX + canvas.Metrics.Gutter.TotalWidth, y + canvas.Metrics.LineHeight),
				canvas.Palette.GutterFill);
		}

		// WARNING: The gutter fill is opaque and drawn after the row's text, so the highlight is drawn
		// again over the gutter once the fill is down, and before the columns the gutter carries.
		void DrawGutterCurrentLine(const RowCanvas& canvas, float y)
		{
			const float gutterX = canvas.Origin.x + canvas.ScrollX;

			canvas.Draw->AddRectFilled(
				ImVec2(gutterX, y),
				ImVec2(gutterX + canvas.Metrics.Gutter.TotalWidth, y + canvas.Metrics.LineHeight),
				canvas.Palette.CurrentLine);
		}

		void DrawDeletionAnchor(const RowCanvas& canvas, float columnX, const DeletionAnchor& anchor,
								float y)
		{
			const ImU32 colour =
				anchor.Staged ? Core::WithAlphaScaled(canvas.Palette.ChangeRemoved, kStagedChangeAlpha)
							  : canvas.Palette.ChangeRemoved;
			const float triangleSize = Core::Scaled(kChangeColumnWidth);

			canvas.Draw->AddTriangleFilled(ImVec2(columnX, y - triangleSize),
										   ImVec2(columnX + triangleSize, y),
										   ImVec2(columnX, y + triangleSize), colour);
		}

		// WARNING: A deletion past the last line is drawn on that line's last row. On the first
		// row of a wrapped last line it would sit between two rows of the same line and the next
		// row's gutter fill would cover half of it.
		void DrawChangeMarker(const RowCanvas& canvas, const RowSlice& slice,
							  const ChangeMarkers* changes, std::size_t lineCount)
		{
			if (changes == nullptr)
			{
				return;
			}

			const float columnX =
				canvas.Origin.x + canvas.ScrollX + canvas.Metrics.Gutter.BreakpointWidth;
			const float top = slice.Y;
			const float bottom = slice.Y + canvas.Metrics.LineHeight;

			// WARNING: The markers may describe a buffer a few edits old, so the line is checked.
			if (slice.Line < changes->Lines.size())
			{
				const LineChange change = changes->Lines[slice.Line];

				if (change.Kind != ChangeKind::None)
				{
					const ImU32 base = change.Kind == ChangeKind::Added ? canvas.Palette.ChangeAdded
																		: canvas.Palette.ChangeModified;
					const ImU32 barColour =
						change.Staged ? Core::WithAlphaScaled(base, kStagedChangeAlpha) : base;

					canvas.Draw->AddRectFilled(
						ImVec2(columnX, top),
						ImVec2(columnX + Core::Scaled(kChangeBarWidth), bottom), barColour);
				}
			}

			if (changes->Deletions.empty())
			{
				return;
			}

			const std::size_t lastLine = lineCount == 0 ? 0 : lineCount - 1;

			const std::vector<DeletionAnchor>::const_iterator anchorOnLine =
				std::ranges::lower_bound(changes->Deletions, slice.Line, {}, &DeletionAnchor::Line);

			if (slice.SubRow == 0 && anchorOnLine != changes->Deletions.end() &&
				anchorOnLine->Line == slice.Line)
			{
				DrawDeletionAnchor(canvas, columnX, *anchorOnLine, top);
			}

			if (slice.LastRowOfLine && slice.Line == lastLine &&
				changes->Deletions.back().Line > lastLine)
			{
				DrawDeletionAnchor(canvas, columnX, changes->Deletions.back(), bottom);
			}
		}

		void DrawGutter(const RowCanvas& canvas, const RowSlice& slice, bool onCaretLine,
						const ChangeMarkers* changes, std::size_t lineCount)
		{
			const float gutterX = canvas.Origin.x + canvas.ScrollX;

			DrawGutterFill(canvas, slice.Y);

			if (onCaretLine)
			{
				DrawGutterCurrentLine(canvas, slice.Y);
			}

			DrawChangeMarker(canvas, slice, changes, lineCount);

			if (slice.SubRow != 0)
			{
				return;
			}

			const std::string number = std::to_string(slice.Line + 1);
			const float numberColumnLeft =
				gutterX + canvas.Metrics.Gutter.BreakpointWidth + canvas.Metrics.Gutter.ChangeWidth;
			const float numberX = canvas.Metrics.Gutter.Numbers.NumberLeft(numberColumnLeft, slice.Line + 1);

			canvas.Draw->AddText(ImVec2(numberX, TextTopOf(canvas.Metrics, slice.Y)),
								 onCaretLine ? canvas.Palette.Text : canvas.Palette.Number,
								 number.c_str());
		}

		bool SelectionPassesThroughLens(const Core::TextBuffer& buffer, const Core::OpenItem& item,
										std::size_t documentRow)
		{
			if (documentRow == kFirstRow)
			{
				return false;
			}

			const auto [selectionFrom, selectionTo] = Core::SelectionRange(item.Caret);

			if (selectionFrom == selectionTo)
			{
				return false;
			}

			const std::size_t boundary = Core::VisualToCaret(
				buffer, item.Rows,
				Core::VisualPosition{.Row = documentRow, .Column = kRowStartColumn}, item.TabWidth);

			return boundary > selectionFrom && boundary < selectionTo;
		}

		void DrawLensRow(const RowCanvas& canvas, const Lens* lens, std::string_view waiting, float y, bool selected,
						 bool hovered)
		{
			if (selected)
			{
				canvas.Draw->AddRectFilled(ImVec2(canvas.TextOriginX, y),
										   ImVec2(canvas.TextOriginX + canvas.Metrics.CellWidth,
												  y + canvas.Metrics.LineHeight),
										   canvas.Palette.Selection);
			}

			const std::string text = lens != nullptr ? LensRowText(*lens, waiting) : std::string{};

			if (!text.empty())
			{
				const float x = LensLabelLeft(*lens, canvas.TextOriginX, canvas.Metrics);
				const ImU32 colour = hovered ? canvas.Palette.Text : canvas.Palette.Lens;

				PushLensFont(canvas.Metrics);

				const float labelHeight = ImGui::GetFontSize();
				const float labelTop = y + CentredTextOffset(canvas.Metrics.LineHeight, labelHeight);

				canvas.Draw->AddText(ImVec2(x, labelTop), colour, text.c_str());

				if (hovered)
				{
					const float underlineY = labelTop + labelHeight - Core::Scaled(kHairlineWidth);
					canvas.Draw->AddLine(
						ImVec2(x, underlineY),
						ImVec2(x + ImGui::CalcTextSize(text.c_str()).x, underlineY), colour,
						Core::Scaled(kHairlineWidth));
				}

				ImGui::PopFont();
			}

			DrawGutterFill(canvas, y);
		}

		void DrawCaret(const RowCanvas& canvas, const RowSlice& slice, std::size_t column,
					   double movedAt)
		{
			const float caretX =
				canvas.TextOriginX + static_cast<float>(column) * canvas.Metrics.CellWidth;

			const double phase = std::fmod(ImGui::GetTime() - movedAt, kCaretBlinkPeriod);

			const double untilFlip =
				phase < kHalfBlinkPeriod ? kHalfBlinkPeriod - phase : kCaretBlinkPeriod - phase;
			Core::Redraw::RequestIn(std::chrono::duration<double>(untilFlip));

			if (phase < kHalfBlinkPeriod)
			{
				canvas.Draw->AddLine(ImVec2(caretX, slice.Y),
									 ImVec2(caretX, slice.Y + canvas.Metrics.LineHeight),
									 canvas.Palette.Caret, kHairlineWidth);
			}
		}

		struct MouseHit
		{
			bool Valid = false;
			std::size_t Row = 0;
			std::string Expanded;
			std::size_t ByteInLine = 0;
		};

		MouseHit ComputeMouseHit(const Core::TextBuffer& buffer, const Core::OpenItem& item,
								 const EditorMetrics& metrics, const ImVec2& origin,
								 float textOriginX, const ScreenRows& screenRows)
		{
			MouseHit hit;

			if (!ImGui::IsWindowHovered())
			{
				return hit;
			}

			const ImVec2 mouse = ImGui::GetMousePos();

			if (mouse.y < origin.y || mouse.x < textOriginX)
			{
				return hit;
			}

			const std::size_t screenRow = static_cast<std::size_t>((mouse.y - origin.y) / metrics.LineHeight);
			const ScreenRows::Row rowHit = screenRows.At(screenRow);

			if (rowHit.Kind != ScreenRows::RowKind::Document)
			{
				return hit;
			}

			const std::size_t row = rowHit.DocumentRow;
			const float relativeX = mouse.x - textOriginX;
			const auto column = static_cast<std::size_t>(
				(relativeX + metrics.CellWidth * kHalf) / metrics.CellWidth);

			const Core::RowRef rowRef = item.Rows.RowAt(row);
			const std::size_t fromColumn = FirstColumnOfRow(item.Rows, rowRef);

			hit.Valid = true;
			hit.Row = row;
			hit.Expanded = Core::ExpandTabs(buffer.LineText(rowRef.Line), item.TabWidth);
			hit.ByteInLine = AdvanceColumns(hit.Expanded, 0, fromColumn + column);

			return hit;
		}

		struct HoverIdentifier
		{
			bool Valid = false;
			std::size_t Row = 0;
			std::size_t FromColumn = 0;
			std::size_t ToColumn = 0;
		};

		HoverIdentifier ComputeHoverIdentifier(const Core::TextBuffer& buffer,
											   const Core::OpenItem& item,
											   const EditorMetrics& metrics, const ImVec2& origin,
											   float textOriginX, const ScreenRows& screenRows,
											   SyntaxCache* syntax)
		{
			HoverIdentifier hover;

			if (syntax == nullptr || !ImGui::GetIO().KeyCtrl)
			{
				return hover;
			}

			const MouseHit hit = ComputeMouseHit(buffer, item, metrics, origin, textOriginX, screenRows);

			if (!hit.Valid)
			{
				return hover;
			}

			const Core::RowRef rowRef = item.Rows.RowAt(hit.Row);

			for (const Token& token : syntax->TokensOf(buffer, rowRef.Line))
			{
				if (!NamesSomething(token.Kind))
				{
					continue;
				}

				if (hit.ByteInLine < token.From || hit.ByteInLine >= token.To)
				{
					continue;
				}

				hover.Valid = true;
				hover.Row = hit.Row;
				hover.FromColumn = Core::ColumnOfByte(hit.Expanded, token.From, item.TabWidth);
				hover.ToColumn = Core::ColumnOfByte(hit.Expanded, token.To, item.TabWidth);
				break;
			}

			return hover;
		}

		void DrawHoverUnderline(const RowCanvas& canvas, const RowSlice& slice,
								const HoverIdentifier& hover)
		{
			if (!hover.Valid || hover.Row != slice.Index)
			{
				return;
			}

			const ColumnSpan clipped = ClipToRow(slice, hover.FromColumn, hover.ToColumn);

			if (clipped.IsEmpty())
			{
				return;
			}

			const float fromX = ColumnX(canvas, slice, clipped.FromColumn);
			const float toX = ColumnX(canvas, slice, clipped.ToColumn);
			const float y = TextBottomOf(canvas.Metrics, slice.Y) - Core::Scaled(kHairlineWidth);

			canvas.Draw->AddLine(ImVec2(fromX, y), ImVec2(toX, y), canvas.Palette.Text,
								 Core::Scaled(kHairlineWidth));
		}

		struct DiagnosticHover
		{
			bool Valid = false;
			std::vector<DiagnosticSpan> Spans;
		};

		DiagnosticHover ComputeDiagnosticHover(const Core::TextBuffer& buffer,
											   const Core::OpenItem& item,
											   const EditorMetrics& metrics, const ImVec2& origin,
											   float textOriginX, const ScreenRows& screenRows,
											   const DiagnosticOverlay* diagnostics)
		{
			DiagnosticHover hover;

			if (diagnostics == nullptr)
			{
				return hover;
			}

			const MouseHit hit = ComputeMouseHit(buffer, item, metrics, origin, textOriginX, screenRows);

			if (!hit.Valid)
			{
				return hover;
			}

			const Core::RowRef rowRef = item.Rows.RowAt(hit.Row);

			for (const DiagnosticSpan& span : diagnostics->SpansOn(rowRef.Line))
			{
				if (hit.ByteInLine < span.From || hit.ByteInLine >= span.To)
				{
					continue;
				}

				hover.Valid = true;
				hover.Spans.push_back(span);
			}

			return hover;
		}

		// WARNING: A tooltip and nothing else. A popup or modal would take focus away from the
		// editor while the pointer merely rests on a squiggle.
		void DrawDiagnosticTooltip(const DiagnosticHover& hover)
		{
			if (!hover.Valid)
			{
				return;
			}

			Core::PushInterfaceFont();
			ImGui::BeginTooltip();
			DrawDiagnosticMessages(hover.Spans);
			ImGui::EndTooltip();
			ImGui::PopFont();
		}

		struct HoverFrame
		{
			HoverState* State = nullptr;
			HoverSettings Settings;
			const Core::FrameInput* Input = nullptr;
			const SemanticOverlay* PreviewOverlay = nullptr;
			PreviewSemantics* Semantics = nullptr;
			std::string_view LoadingText;
		};

		// WARNING: The syntax cache's tokens are ranges of the tab-expanded line, so they are
		// moved onto the raw line rather than the raw line being lexed afresh. The cached tokens
		// were lexed in the state the lines above leave, so a word inside a block comment opened
		// higher up is still a comment.
		std::optional<HoverToken> ComputeHoverToken(const Core::TextBuffer& buffer,
													const Core::OpenItem& item,
													const EditorMetrics& metrics,
													const ImVec2& origin, float textOriginX,
													const ScreenRows& screenRows, SyntaxCache* syntax)
		{
			if (syntax == nullptr)
			{
				return std::nullopt;
			}

			const MouseHit hit = ComputeMouseHit(buffer, item, metrics, origin, textOriginX, screenRows);

			if (!hit.Valid)
			{
				return std::nullopt;
			}

			const Core::RowRef rowRef = item.Rows.RowAt(hit.Row);
			const std::string raw = buffer.LineText(rowRef.Line);
			const std::vector<Token> rawTokens = RawTokensOf(
				raw, hit.Expanded, syntax->TokensOf(buffer, rowRef.Line), item.TabWidth);

			const std::size_t rawByte =
				RawByteOfExpanded(raw, hit.Expanded, hit.ByteInLine, item.TabWidth);

			return TokenUnderByte(raw, rowRef.Line, rawByte, rawTokens);
		}

		std::optional<HoverQuestion> HoverQuestionFor(const Core::TextBuffer& buffer,
													  const std::filesystem::path& path,
													  const std::optional<HoverToken>& token)
		{
			if (!token || token->Line >= buffer.GetLineCount())
			{
				return std::nullopt;
			}

			const std::string raw = buffer.LineText(token->Line);

			HoverQuestion question;
			question.Path = path;
			question.DocumentRevision = buffer.GetRevision();
			question.Line = token->Line;
			question.TokenFrom = token->From;
			question.TokenTo = token->To;
			question.From = PositionOfToken(path, raw, *token);

			return question;
		}

		HoverAnchor AnchorOfToken(const Core::TextBuffer& buffer, const Core::OpenItem& item,
								  const EditorMetrics& metrics, const ImVec2& origin,
								  float textOriginX, const ScreenRows& screenRows,
								  const HoverToken& token)
		{
			const std::string raw = buffer.LineText(token.Line);
			const Core::VisualPosition visual = Core::CaretToVisual(
				buffer, item.Rows, buffer.LineStart(token.Line) + std::min(token.From, raw.size()),
				item.TabWidth);
			const std::size_t width = Core::ColumnOfByte(raw, token.To, item.TabWidth) -
									  Core::ColumnOfByte(raw, token.From, item.TabWidth);

			HoverAnchor anchor;
			anchor.Min = ImVec2(textOriginX + static_cast<float>(visual.Column) * metrics.CellWidth,
								origin.y + static_cast<float>(screenRows.ScreenRowOf(visual.Row)) *
											   metrics.LineHeight);
			anchor.Max = ImVec2(anchor.Min.x + static_cast<float>(width) * metrics.CellWidth,
								anchor.Min.y + metrics.LineHeight);

			return anchor;
		}

		bool IsLoneModifier(Core::KeyCode key)
		{
			switch (key)
			{
			case Core::KeyCode::LeftShift:
			case Core::KeyCode::LeftControl:
			case Core::KeyCode::LeftAlt:
			case Core::KeyCode::LeftSuper:
			case Core::KeyCode::RightShift:
			case Core::KeyCode::RightControl:
			case Core::KeyCode::RightAlt:
			case Core::KeyCode::RightSuper:
				return true;
			default:
				return false;
			}
		}

		bool AnyNamedKeyPressed()
		{
			for (int keyIndex = ImGuiKey_NamedKey_BEGIN; keyIndex < ImGuiKey_NamedKey_END; ++keyIndex)
			{
				const auto key = static_cast<ImGuiKey>(keyIndex);

				if (Core::IsKeyboardKey(key) && !Core::IsModifierKey(key) &&
					ImGui::IsKeyPressed(key, kNotRepeating))
				{
					return true;
				}
			}

			return false;
		}

		bool AnyMouseButtonClicked()
		{
			for (int mouseButton = 0; mouseButton < ImGuiMouseButton_COUNT; ++mouseButton)
			{
				if (ImGui::IsMouseClicked(mouseButton))
				{
					return true;
				}
			}

			return false;
		}

		// WARNING: ImGui's key state is read as well as FrameInput. FrameInput carries only the
		// keys nobody upstream consumed, so F12, Alt+O or Ctrl+S never reach it, and the tooltip
		// would outlive the jump or popup such a shortcut opens.
		bool HoverInputHappened(const Core::FrameInput* input, bool mouseInTooltip, bool edited)
		{
			if (edited)
			{
				return true;
			}

			if (AnyNamedKeyPressed())
			{
				return true;
			}

			if (input != nullptr)
			{
				for (const Core::KeyStroke& stroke : input->GetStrokes())
				{
					if (!IsLoneModifier(stroke.Key))
					{
						return true;
					}
				}
			}

			if (!mouseInTooltip && AnyMouseButtonClicked())
			{
				return true;
			}

			const ImGuiIO& io = ImGui::GetIO();

			return (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) && ImGui::IsWindowHovered();
		}

		void RunHover(const Core::TextBuffer& buffer, const Core::OpenItem& item,
					  const RowCanvas& canvas, const ScreenRows& screenRows, SyntaxCache* syntax,
					  const DiagnosticHover& diagnostic, const HoverFrame& frame,
					  EditorFrameResult& result)
		{
			HoverState& hover = *frame.State;
			const EditorMetrics& metrics = canvas.Metrics;
			const ImVec2 mouse = ImGui::GetIO().MousePos;

			// WARNING: The tooltip is submitted after the surface, so last frame's rectangle is
			// used here. One frame of lag on a rectangle the mouse has to travel into is invisible.
			const bool mouseInTooltip = hover.Tooltip.Contains(mouse);
			const bool mouseMoved = mouse.x != hover.LastMouse.x || mouse.y != hover.LastMouse.y;

			hover.LastMouse = mouse;

			HoverInput input;
			input.Token = ComputeHoverToken(buffer, item, metrics, canvas.Origin,
											canvas.TextOriginX, screenRows, syntax);
			input.MouseInTooltip = mouseInTooltip;
			input.MouseMoved = mouseMoved;
			input.Ctrl = ImGui::GetIO().KeyCtrl;
			input.DiagnosticUnderMouse = diagnostic.Valid;
			input.InputHappened = HoverInputHappened(frame.Input, mouseInTooltip, result.Edited);
			input.Revision = buffer.GetRevision();
			input.Settings = frame.Settings;
			input.Now = ImGui::GetTime();

			// WARNING: Keyed on the tracker's own token as of its last Update, not on the token
			// under the mouse. During the leave grace and while the mouse is in the tooltip the two
			// differ, and the answers shown belong to the tracker's.
			const std::optional<HoverQuestion> trackedQuestion =
				HoverQuestionFor(buffer, item.Path, hover.Tracker.Current());

			input.InfoArrived = trackedQuestion.has_value() && hover.InfoFor == trackedQuestion;
			input.PreviewArrived = trackedQuestion.has_value() && hover.PreviewFor == trackedQuestion;
			input.InfoFailed = trackedQuestion.has_value() && hover.InfoFailedFor == trackedQuestion;
			input.PreviewFailed =
				trackedQuestion.has_value() && hover.PreviewFailedFor == trackedQuestion;
			input.InfoFailureSaysWhy = !hover.InfoFailureText.empty();
			input.PreviewFailureSaysWhy = !hover.PreviewFailureText.empty();

			HoverActions actions = hover.Tracker.Update(input);

			const std::optional<HoverToken> current = hover.Tracker.Current();
			const std::optional<HoverQuestion> question =
				HoverQuestionFor(buffer, item.Path, current);

			if (actions.Cancel)
			{
				result.CancelHover = true;
			}

			// WARNING: A question asked again is no longer failed; until it answers, the tracker
			// must not read the old failure as the new question's.
			if (actions.AskInfo && question)
			{
				result.AskHover = question;
				hover.InfoFailedFor.reset();
			}

			if (actions.AskPreview && question)
			{
				result.AskPreview = question;
				hover.PreviewFailedFor.reset();
			}

			if (actions.WakeAt)
			{
				Core::Redraw::RequestIn(
					std::chrono::duration<double>(std::max(0.0, *actions.WakeAt - input.Now)));
			}

			actions.ShowInfo =
				actions.ShowInfo && question.has_value() && hover.InfoFor == question;
			actions.ShowPreview =
				actions.ShowPreview && question.has_value() && hover.PreviewFor == question;
			actions.InfoLoading = actions.InfoLoading && question.has_value();
			actions.PreviewLoading = actions.PreviewLoading && question.has_value();
			actions.InfoGaveUp = actions.InfoGaveUp && question.has_value() && hover.InfoFailedFor == question;
			actions.PreviewGaveUp = actions.PreviewGaveUp && question.has_value() && hover.PreviewFailedFor == question;

			// WARNING: Refreshed only while the mouse is still where the tracker's token is. During
			// the grace it may be crossing some other squiggle on its way to the tooltip.
			if (diagnostic.Valid && input.Token == current)
			{
				hover.HeldDiagnostics = diagnostic.Spans;
			}

			if (current && current->Line < buffer.GetLineCount())
			{
				const HoverAnchor anchor = AnchorOfToken(buffer, item, metrics, canvas.Origin,
														 canvas.TextOriginX, screenRows, *current);
				hover.AnchorMin = anchor.Min;
				hover.AnchorMax = anchor.Max;
			}
			else if (diagnostic.Valid)
			{
				const float rowUnderMouse =
					std::floor((mouse.y - canvas.Origin.y) / metrics.LineHeight);
				const float rowTop = canvas.Origin.y + rowUnderMouse * metrics.LineHeight;

				hover.AnchorMin = ImVec2(mouse.x, rowTop);
				hover.AnchorMax = ImVec2(mouse.x, rowTop + metrics.LineHeight);
			}

			HoverTooltipLook look;
			look.Palette = canvas.Palette;
			look.Syntax = canvas.SyntaxColours;

			// WARNING: The tooltip's code rows are one text line tall, not an editor line. The overload
			// note is laid beside a declaration's row with SameLine, at the row's top, so code centred in a
			// taller row would sit below its note. Only the anchor takes the editor line.
			look.LineHeight = metrics.FontSize;
			look.CellWidth = metrics.CellWidth;
			look.TabWidth = item.TabWidth;
			look.Language = LanguageOfPath(item.Path);
			look.Viewport = ImGui::GetWindowViewport();

			const ImGuiWindow* const surface = ImGui::GetCurrentWindow();
			look.BoundsMin = surface->InnerRect.Min;
			look.BoundsMax = surface->InnerRect.Max;
			look.PreviewOverlay = frame.PreviewOverlay;
			look.Semantics = frame.Semantics;
			look.LoadingText = std::string{frame.LoadingText};

			DrawHoverTooltip(actions, hover, hover.HeldDiagnostics, look,
							 HoverAnchor{hover.AnchorMin, hover.AnchorMax},
							 ImGui::GetCurrentWindow());
		}

		// WARNING: Tested against the rectangle the author was drawn in last frame. This frame's is drawn with the
		// rows, after the mouse has been given to the caret, and a click on the text must not move the caret.
		AuthorCardInput ReadAuthorCardInput(const PointerState& pointer, const std::optional<ImRect>& authorRect,
											const InteractiveTooltipState& card, std::size_t caretLine,
											const Core::FrameInput* input)
		{
			constexpr bool kNotEdited = false;

			const ImVec2 mouse = ImGui::GetIO().MousePos;
			const bool selecting = pointer.Held && !pointer.PressedThisFrame;

			AuthorCardInput cardInput;
			cardInput.TextShown = authorRect.has_value();
			cardInput.OverText = authorRect.has_value() && pointer.Hovered && !selecting && authorRect->Contains(mouse);
			cardInput.OverCard = card.Contains(mouse);
			cardInput.ClickedText = cardInput.OverText && pointer.PressedThisFrame;
			cardInput.Dismissed = HoverInputHappened(input, cardInput.OverCard || cardInput.ClickedText, kNotEdited);
			cardInput.Line = caretLine;
			cardInput.Seconds = ImGui::GetIO().DeltaTime;

			return cardInput;
		}

		void RunAuthorCard(const LineAuthorView& author, std::size_t caretLine, EditorWidgetState& widget,
						   EditorFrameResult& result)
		{
			const AuthorCardTracker& tracker = widget.AuthorCard;
			const std::optional<std::size_t> line = tracker.ShownLine();

			std::optional<AuthorCardContent> content;

			// The card hangs off the text drawn this frame, so it is not drawn for a line the caret has left.
			if (line && *line == caretLine && widget.AuthorTextRect && author.Authors != nullptr)
			{
				content = BuildAuthorCard(*author.Authors, *line, author.Now, author.Zone);
			}

			if (content)
			{
				ImGuiWindow* const surface = ImGui::GetCurrentWindow();

				const InteractiveTooltipPlace place{.Viewport = ImGui::GetWindowViewport(),
													.BoundsMin = surface->InnerRect.Min,
													.BoundsMax = surface->InnerRect.Max,
													.AnchorMin = widget.AuthorTextRect->Min,
													.AnchorMax = widget.AuthorTextRect->Max};

				if (const std::optional<AuthorCardAction::Kind> clicked =
						DrawAuthorCard(*content, widget.AuthorCardWindow, place, surface))
				{
					result.CardAction = AuthorCardAction{.Action = *clicked, .Line = *line};
				}
			}
			else
			{
				widget.AuthorCardWindow.Shown = false;
			}

			// WARNING: The loop idles between frames, so the frames that end the hover delay and the leave grace
			// have to be asked for.
			if (const std::optional<float> untilShown = tracker.SecondsUntilShown())
			{
				Core::Redraw::RequestIn(std::chrono::duration<double>(*untilShown));
			}

			if (const std::optional<float> untilHidden = tracker.SecondsUntilHidden())
			{
				Core::Redraw::RequestIn(std::chrono::duration<double>(*untilHidden));
			}
		}

		constexpr float kChangeHoverSlack = 3.0f;

		constexpr std::size_t kChangeTooltipLineBytes = 160;

		// WARNING: A deletion's triangle sits on the boundary between two lines and overlaps the
		// bars on either side, so it is tested first: within its own height of the boundary the
		// pointer is over the triangle.
		const ChangeHunk* ComputeChangeHover(const Core::OpenItem& item, const RowCanvas& canvas,
											 const ScreenRows& screenRows, const ChangeMarkers* changes)
		{
			if (changes == nullptr || changes->Hunks.empty() || !ImGui::IsWindowHovered())
			{
				return nullptr;
			}

			const ImVec2 mouse = ImGui::GetMousePos();
			const float columnX =
				canvas.Origin.x + canvas.ScrollX + canvas.Metrics.Gutter.BreakpointWidth;

			if (mouse.x < columnX - Core::Scaled(kChangeHoverSlack) ||
				mouse.x > columnX + Core::Scaled(kChangeColumnWidth) +
							  Core::Scaled(kChangeHoverSlack) ||
				mouse.y < canvas.Origin.y)
			{
				return nullptr;
			}

			const float relativeY = mouse.y - canvas.Origin.y;
			const auto screenRow = static_cast<std::size_t>(relativeY / canvas.Metrics.LineHeight);
			const float offsetInRow =
				relativeY - static_cast<float>(screenRow) * canvas.Metrics.LineHeight;

			const ScreenRows::Row hit = screenRows.At(screenRow);

			if (hit.Kind != ScreenRows::RowKind::Document || hit.DocumentRow >= item.Rows.GetTotalRows())
			{
				return nullptr;
			}

			const Core::RowRef rowRef = item.Rows.RowAt(hit.DocumentRow);
			const bool lastRowOfLine = rowRef.SubRow + 1 >= item.Rows.RowsOf(rowRef.Line);

			if (rowRef.SubRow == 0 && offsetInRow <= Core::Scaled(kChangeColumnWidth))
			{
				if (const ChangeHunk* above = FindDeletionHunk(*changes, rowRef.Line))
				{
					return above;
				}
			}

			// WARNING: A lens or peek row between this line and the next holds the upper half of the
			// next line's triangle, so the bottom of this row is then only this line's bar.
			if (lastRowOfLine &&
				offsetInRow >= canvas.Metrics.LineHeight - Core::Scaled(kChangeColumnWidth) &&
				screenRows.At(screenRow + 1).Kind == ScreenRows::RowKind::Document)
			{
				if (const ChangeHunk* below = FindDeletionHunk(*changes, rowRef.Line + 1))
				{
					return below;
				}

				// WARNING: Past the last line the triangle drawn is whichever anchor lies beyond
				// it, even one a shrinking buffer has left further out than the line after.
				const bool lastLine = rowRef.Line + 1 >= item.Rows.GetLineCount();

				if (lastLine && !changes->Deletions.empty() &&
					changes->Deletions.back().Line > rowRef.Line)
				{
					if (const ChangeHunk* beyond =
							FindDeletionHunk(*changes, changes->Deletions.back().Line))
					{
						return beyond;
					}
				}
			}

			return FindLineHunk(*changes, rowRef.Line);
		}

		enum class OldLinePiece
		{
			Kept,
			Removed
		};

		constexpr float kNoSameLineOffset = 0.0f;
		constexpr float kNoSameLineSpacing = 0.0f;

		void DrawOldLinePiece(std::string_view text, OldLinePiece kind, const EditorPalette& palette,
							  std::size_t tabWidth)
		{
			if (text.empty())
			{
				return;
			}

			const std::string expanded = Core::ExpandTabs(text, tabWidth);

			ImGui::SameLine(kNoSameLineOffset, kNoSameLineSpacing);

			if (kind == OldLinePiece::Removed)
			{
				const ImVec2 pieceOrigin = ImGui::GetCursorScreenPos();
				const ImVec2 size =
					ImGui::CalcTextSize(expanded.c_str(), expanded.c_str() + expanded.size());

				ImGui::GetWindowDrawList()->AddRectFilled(
					pieceOrigin, ImVec2(pieceOrigin.x + size.x, pieceOrigin.y + size.y),
					palette.ChangeOldRemoved);
			}

			ImGui::TextUnformatted(expanded.c_str(), expanded.c_str() + expanded.size());
		}

		// WARNING: Tabs are expanded per piece rather than across the line, so a tab after the
		// removed part may land a column off from where the file puts it. The tooltip is read,
		// not aligned against anything.
		void DrawOldLine(std::string_view line, ChangedSpan removed, const EditorPalette& palette,
						 std::size_t tabWidth)
		{
			bool cut = false;

			if (line.size() > kChangeTooltipLineBytes)
			{
				const std::size_t end = Core::CharacterBoundaryAtOrBefore(line, kChangeTooltipLineBytes);

				line = line.substr(0, end);
				cut = true;
			}

			removed.From = std::min(removed.From, line.size());
			removed.To = std::min(removed.To, line.size());

			ImGui::PushStyleColor(ImGuiCol_Text, palette.ChangeOldText);
			ImGui::TextUnformatted("- ");

			DrawOldLinePiece(line.substr(0, removed.From), OldLinePiece::Kept, palette, tabWidth);
			DrawOldLinePiece(line.substr(removed.From, removed.To - removed.From),
							 OldLinePiece::Removed, palette, tabWidth);
			DrawOldLinePiece(line.substr(removed.To), OldLinePiece::Kept, palette, tabWidth);

			if (cut)
			{
				DrawOldLinePiece(kEllipsis, OldLinePiece::Kept, palette, tabWidth);
			}

			ImGui::PopStyleColor();
		}

		void DrawChangeTooltip(const ChangeHunk* hunk, const Core::TextBuffer& buffer,
							   const EditorPalette& palette, std::size_t tabWidth)
		{
			if (hunk == nullptr || !ImGui::BeginTooltip())
			{
				return;
			}

			Core::PushInterfaceFont();
			ImGui::TextUnformatted(DescribeHunk(*hunk).c_str());
			ImGui::PopFont();

			for (std::size_t index = 0; index < hunk->OldLines.size(); ++index)
			{
				const std::string& oldLine = hunk->OldLines[index];
				const std::size_t wholeLineEnd = hunk->Kind == HunkKind::Deleted ? 0 : oldLine.size();
				ChangedSpan removed{.From = 0, .To = wholeLineEnd};

				const std::size_t partner = hunk->FirstLine + index;

				// WARNING: Paired only while the hunk's new lines sit where it put them. Once
				// unstaged edits have split or rewritten a staged hunk, the line in position k is
				// not its k-th new line, and comparing against it would highlight almost everything.
				const bool pairsWithBufferLine = hunk->Kind == HunkKind::Modified && hunk->Intact &&
												 index < hunk->LineCount &&
												 partner < buffer.GetLineCount();

				if (pairsWithBufferLine)
				{
					removed = RemovedSpan(oldLine, buffer.LineText(partner));
				}

				DrawOldLine(oldLine, removed, palette, tabWidth);
			}

			if (hunk->OldCount > hunk->OldLines.size())
			{
				Core::PushInterfaceFont();
				ImGui::TextDisabled("… and %zu more", hunk->OldCount - hunk->OldLines.size());
				ImGui::PopFont();
			}

			ImGui::EndTooltip();
		}

		constexpr float kOverviewMinHeight = 2.0f;

		constexpr float kOverviewMarkWidth = 4.0f;

		constexpr float kOccurrenceTickGap = 1.0f;

		constexpr float kOverviewCaretHeight = 2.0f;

		constexpr float kBothTrackEdges = 2.0f;
		constexpr float kLeastTrackHeight = 1.0f;

		// WARNING: Marks are placed against the track the grab moves within, so a mark sits where
		// the grab stands when its row is at the top of the view. Once a very long file shrinks the
		// grab to its minimum size the two drift apart towards the bottom by up to that size.
		struct OverviewTrack
		{
			ImRect Bar;
			float Inset = 0.0f;
			float Top = 0.0f;
			float Height = 0.0f;
			float Content = 0.0f;

			float RowFraction(std::size_t screenRow, float lineHeight) const
			{
				return static_cast<float>(screenRow) * lineHeight / Content;
			}
		};

		std::optional<OverviewTrack> OverviewTrackOf(ImGuiWindow* window)
		{
			if (!window->ScrollbarY)
			{
				return std::nullopt;
			}

			OverviewTrack track;

			track.Bar = ImGui::GetWindowScrollbarRect(window, ImGuiAxis_Y);

			// WARNING: ImGui fades its own grab out of a bar shorter than it is wide, so there is
			// no track worth marking.
			if (track.Bar.GetHeight() < track.Bar.GetWidth())
			{
				return std::nullopt;
			}

			track.Inset = Core::ScrollbarGrabInset(ImGui::GetStyle().ScrollbarPadding, track.Bar.GetSize());
			track.Top = track.Bar.Min.y + track.Inset;
			track.Height = track.Bar.GetHeight() - track.Inset * kBothTrackEdges;
			track.Content = std::max(window->ContentSize.y + window->WindowPadding.y * kBothTrackEdges,
									 window->InnerRect.GetHeight());

			if (track.Height < kLeastTrackHeight || track.Content <= 0.0f)
			{
				return std::nullopt;
			}

			return track;
		}

		// WARNING: The window's own clip rectangle stops short of its scrollbar, so the bar gets
		// its own, kept within what of the window is actually visible.
		void PushOverviewClip(ImGuiWindow* window, const OverviewTrack& track)
		{
			constexpr bool kIntersectWithCurrentClip = false;

			ImRect clip = track.Bar;
			clip.ClipWith(window->OuterRectClipped);
			window->DrawList->PushClipRect(clip.Min, clip.Max, kIntersectWithCurrentClip);
		}

		struct OverviewStrip
		{
			float Left = 0.0f;
			float Right = 0.0f;
		};

		OverviewStrip CentreStripOf(const OverviewTrack& track)
		{
			const float centre = (track.Bar.Min.x + track.Bar.Max.x) * kHalf;
			const float halfWidth =
				ImMin(Core::Scaled(kOverviewMarkWidth), track.Bar.GetWidth() - track.Inset * kBothTrackEdges) * kHalf;
			const float left = IM_TRUNC(centre - halfWidth);

			return OverviewStrip{.Left = left, .Right = left + halfWidth * kBothTrackEdges};
		}

		float OverviewLineTop(const Core::OpenItem& item, const RowCanvas& canvas,
							  const ScreenRows& screenRows, const OverviewTrack& track, std::size_t line)
		{
			const std::size_t lineCount = item.Rows.GetLineCount();
			const std::size_t totalRows = item.Rows.GetTotalRows();

			const std::size_t screenRow = line >= lineCount
											  ? screenRows.TotalScreenRows(totalRows)
											  : screenRows.ScreenRowOf(item.Rows.FirstRowOf(line));

			return track.RowFraction(screenRow, canvas.Metrics.LineHeight);
		}

		// WARNING: Not the next line's top. Under a line with a peek, the next line's top is the peek's height further
		// down, and a mark ending there stretched over the whole peek.
		float OverviewLineEnd(const Core::OpenItem& item, const RowCanvas& canvas,
							  const ScreenRows& screenRows, const OverviewTrack& track, std::size_t line)
		{
			if (line >= item.Rows.GetLineCount())
			{
				return OverviewLineTop(item, canvas, screenRows, track, line);
			}

			const std::size_t lastRow = item.Rows.FirstRowOf(line) + std::max<std::size_t>(item.Rows.RowsOf(line), 1) - 1;

			return track.RowFraction(screenRows.ScreenRowOf(lastRow) + 1, canvas.Metrics.LineHeight);
		}

		ImU32 OverviewColourOf(HunkKind kind, const EditorPalette& palette)
		{
			if (kind == HunkKind::Added)
			{
				return palette.ChangeAdded;
			}

			if (kind == HunkKind::Modified)
			{
				return palette.ChangeModified;
			}

			return palette.ChangeRemoved;
		}

		void DrawChangeOverview(const Core::OpenItem& item, const RowCanvas& canvas,
								const ScreenRows& screenRows, const ChangeMarkers* changes,
								const OverviewTrack& track)
		{
			if (changes == nullptr || changes->Hunks.empty())
			{
				return;
			}

			ImGuiWindow* window = ImGui::GetCurrentWindow();

			const auto lineTop = [&](std::size_t line) { return OverviewLineTop(item, canvas, screenRows, track, line); };
			const auto lineEnd = [&](std::size_t line) { return OverviewLineEnd(item, canvas, screenRows, track, line); };

			const std::vector<OverviewMark> marks = BuildChangeOverview(
				*changes, lineTop, lineEnd, Core::Scaled(kOverviewMinHeight) / track.Height);

			const OverviewStrip strip = CentreStripOf(track);

			PushOverviewClip(window, track);

			for (const OverviewMark& mark : marks)
			{
				const ImU32 base = OverviewColourOf(mark.Kind, canvas.Palette);

				window->DrawList->AddRectFilled(
					ImVec2(strip.Left, track.Top + mark.From * track.Height),
					ImVec2(strip.Right, track.Top + mark.To * track.Height),
					mark.Staged ? Core::WithAlphaScaled(base, kStagedChangeAlpha) : base);
			}

			window->DrawList->PopClipRect();
		}

		// Ticked in the margins either side of the grab and the centre strip, so they cover neither. A
		// wide scrollbar's grab is wider than the strip.
		void DrawOccurrenceOverview(const Core::OpenItem& item, const RowCanvas& canvas,
									const ScreenRows& screenRows, const SelectionOccurrences& occurrences,
									const OverviewTrack& track)
		{
			if (occurrences.Lines.empty())
			{
				return;
			}

			ImGuiWindow* window = ImGui::GetCurrentWindow();

			const auto lineTop = [&](std::size_t line) { return OverviewLineTop(item, canvas, screenRows, track, line); };
			const auto lineEnd = [&](std::size_t line) { return OverviewLineEnd(item, canvas, screenRows, track, line); };

			const std::vector<OccurrenceMark> marks = BuildOccurrenceOverview(
				occurrences.Lines, lineTop, lineEnd, Core::Scaled(kOverviewMinHeight) / track.Height);

			const OverviewStrip strip = CentreStripOf(track);
			const float gap = Core::Scaled(kOccurrenceTickGap);
			const float leftTickRight = std::min(strip.Left, track.Bar.Min.x + track.Inset) - gap;
			const float rightTickLeft = std::max(strip.Right, track.Bar.Max.x - track.Inset) + gap;
			const ImU32 color = Core::WithOpaqueAlpha(canvas.Palette.SelectionOccurrence);

			PushOverviewClip(window, track);

			for (const OccurrenceMark& mark : marks)
			{
				const float top = track.Top + mark.From * track.Height;
				const float bottom = track.Top + mark.To * track.Height;

				if (leftTickRight > track.Bar.Min.x)
				{
					window->DrawList->AddRectFilled(ImVec2(track.Bar.Min.x, top), ImVec2(leftTickRight, bottom), color);
				}

				if (rightTickLeft < track.Bar.Max.x)
				{
					window->DrawList->AddRectFilled(ImVec2(rightTickLeft, top), ImVec2(track.Bar.Max.x, bottom), color);
				}
			}

			window->DrawList->PopClipRect();
		}

		void DrawCaretOverview(const RowCanvas& canvas, std::size_t caretScreenRow,
							   const OverviewTrack& track)
		{
			ImGuiWindow* window = ImGui::GetCurrentWindow();

			const float fraction =
				ImMin(track.RowFraction(caretScreenRow, canvas.Metrics.LineHeight), 1.0f);
			const float top = IM_TRUNC(track.Top + fraction * track.Height -
									   Core::Scaled(kOverviewCaretHeight) * kHalf);

			PushOverviewClip(window, track);

			window->DrawList->AddRectFilled(
				ImVec2(track.Bar.Min.x + track.Inset, top),
				ImVec2(track.Bar.Max.x - track.Inset, top + Core::Scaled(kOverviewCaretHeight)),
				canvas.Palette.Caret);

			window->DrawList->PopClipRect();
		}

		// WARNING: Built as one polyline rather than a run of AddLine calls, so a long span costs
		// one draw command instead of one per bump.
		void DrawSquiggle(ImDrawList* draw, float fromX, float toX, float baselineY, ImU32 colour)
		{
			if (toX <= fromX)
			{
				return;
			}

			constexpr float kHalfPeriod = 3.0f;
			constexpr float kAmplitude = 1.5f;
			constexpr std::size_t kEndpointPoints = 2;

			std::vector<ImVec2> points;
			points.reserve(static_cast<std::size_t>((toX - fromX) / Core::Scaled(kHalfPeriod)) +
						   kEndpointPoints);
			points.push_back(ImVec2(fromX, baselineY));

			float x = fromX;
			bool crest = true;

			while (x < toX)
			{
				x = std::min(x + Core::Scaled(kHalfPeriod), toX);
				const float waveY = crest ? baselineY - Core::Scaled(kAmplitude)
										  : baselineY + Core::Scaled(kAmplitude);
				points.push_back(ImVec2(x, waveY));
				crest = !crest;
			}

			draw->AddPolyline(points.data(), Core::Narrow<int>(points.size()), colour,
							  ImDrawFlags_None, kHairlineWidth);
		}

		void DrawRowDiagnostics(const RowCanvas& canvas, const RowSlice& slice,
								const VisibleLineCache& cache,
								const std::vector<DiagnosticSpan>& spans, bool stale,
								std::size_t tabWidth)
		{
			constexpr float kSquiggleBottomInset = 2.0f;

			for (const DiagnosticSpan& span : spans)
			{
				const std::size_t fromColumn = Core::ColumnOfByte(cache.Expanded, span.From, tabWidth);
				std::size_t toColumn = Core::ColumnOfByte(cache.Expanded, span.To, tabWidth);

				// WARNING: DiagnosticOverlay widens a zero-length span by one byte. When that byte
				// lands past the end of the line, which is where a missing semicolon or brace is
				// reported, ColumnOfByte clamps it back onto fromColumn and the widening is lost.
				// It is redone here in column space, where one past the last column is a valid end.
				if (toColumn <= fromColumn && span.To > span.From)
				{
					toColumn = fromColumn + 1;
				}

				const ColumnSpan clipped = ClipToRow(slice, fromColumn, toColumn);

				if (clipped.IsEmpty())
				{
					continue;
				}

				const float fromX = ColumnX(canvas, slice, clipped.FromColumn);
				const float toX = ColumnX(canvas, slice, clipped.ToColumn);

				const float baselineY =
					TextBottomOf(canvas.Metrics, slice.Y) - Core::Scaled(kSquiggleBottomInset);

				const ImU32 base = span.Severity == DiagnosticSeverity::Error
									   ? canvas.Palette.DiagnosticError
									   : canvas.Palette.DiagnosticWarning;
				const ImU32 colour =
					stale ? Core::WithAlphaScaled(base, kStaleDiagnosticAlpha) : base;

				DrawSquiggle(canvas.Draw, fromX, toX, baselineY, colour);
			}
		}

		float WidthInCurrentFont(std::string_view text)
		{
			return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x;
		}

		// Where the row's text ends, counted the way DrawRowText advances across it.
		std::size_t EndColumnOfRow(const RowSlice& slice, const VisibleLineCache& cache, std::size_t tabWidth)
		{
			if (slice.FromByte >= slice.ToByte)
			{
				return slice.FromColumn;
			}

			const std::string_view rowText(cache.Expanded.data() + slice.FromByte, slice.ToByte - slice.FromByte);

			return slice.FromColumn + Core::ColumnCount(rowText, tabWidth);
		}

		struct CaretPaint
		{
			std::size_t Line = 0;
			Core::VisualPosition Visual;
			bool Focused = false;
			double MovedAt = 0.0;
		};

		// WARNING: The table can describe a buffer a few edits old, and the caret can then sit on a line past its
		// end, so the line is checked against the table and not only against the buffer.
		std::optional<ImRect> DrawLineAuthor(const RowCanvas& canvas, const RowSlice& slice,
											 const VisibleLineCache& cache, const LineAuthorView& author,
											 const CaretPaint& caret, std::size_t tabWidth)
		{
			const bool atCaretLineEnd = slice.Line == caret.Line && slice.LastRowOfLine;

			if (author.Authors == nullptr || author.Hidden || !atCaretLineEnd ||
				slice.Line >= author.Authors->Lines.size())
			{
				return std::nullopt;
			}

			const float left = ColumnX(canvas, slice, EndColumnOfRow(slice, cache, tabWidth)) +
							   kAuthorGapCells * canvas.Metrics.CellWidth;
			const float visibleRight = canvas.Draw->GetClipRectMax().x;

			if (visibleRight <= left)
			{
				return std::nullopt;
			}

			const std::optional<AuthorTextParts> parts = AuthorTextPartsOf(*author.Authors, slice.Line, author.Now);

			if (!parts)
			{
				return std::nullopt;
			}

			PushLensFont(canvas.Metrics);

			const std::optional<std::string> text = FitAuthorText(*parts, visibleRight - left, WidthInCurrentFont);
			std::optional<ImRect> drawn;

			if (text)
			{
				const float top = slice.Y + CentredTextOffset(canvas.Metrics.LineHeight, ImGui::GetFontSize());
				const float right = left + WidthInCurrentFont(*text);

				canvas.Draw->AddText(ImVec2(left, top), canvas.Palette.Lens, text->data(), text->data() + text->size());

				// The gutter is painted over whatever part has scrolled under it, so that part is not offered.
				const float textAreaLeft = canvas.Origin.x + canvas.ScrollX + canvas.Metrics.Gutter.TotalWidth;

				if (right > textAreaLeft)
				{
					drawn = ImRect(std::max(left, textAreaLeft), slice.Y, right, slice.Y + canvas.Metrics.LineHeight);
				}
			}

			ImGui::PopFont();

			return drawn;
		}

		void DrawRow(const RowCanvas& canvas, const Core::OpenItem& item,
					 const VisibleLineCache& cache, const RowSlice& slice,
					 const std::vector<Token>& tokens, const std::vector<SemanticSpan>& semantic,
					 const std::vector<DiagnosticSpan>& diagnostics, bool diagnosticsStale,
					 const ChangeMarkers* changes, const CaretPaint& caret,
					 const HoverIdentifier& hover, const LineAuthorView& author,
					 EditorWidgetState& widget)
		{
			if (slice.Line == caret.Line)
			{
				DrawCurrentLineHighlight(canvas, slice);
			}

			const std::size_t depth =
				slice.Line < item.IndentDepths.size() ? item.IndentDepths[slice.Line] : 0;

			DrawIndentGuides(canvas, slice, depth, item.TabWidth);

			DrawRowOccurrences(canvas, slice, cache);
			DrawRowMatches(canvas, slice, cache);
			DrawRowBrackets(canvas, slice, cache);

			if (cache.SelectionOnLine)
			{
				DrawRowSelection(canvas, slice, cache);
			}

			DrawRowCurrentMatchOutline(canvas, slice, cache);

			DrawRowText(canvas, slice, cache, tokens, semantic, item.TabWidth);
			DrawRowDiagnostics(canvas, slice, cache, diagnostics, diagnosticsStale, item.TabWidth);

			if (const std::optional<ImRect> authorRect = DrawLineAuthor(canvas, slice, cache, author, caret, item.TabWidth))
			{
				widget.AuthorTextRect = authorRect;
			}

			DrawGutter(canvas, slice, slice.Line == caret.Line, changes,
					   item.Document->GetBuffer().GetLineCount());
			DrawHoverUnderline(canvas, slice, hover);

			const bool caretIsOnThisRow = slice.Index == caret.Visual.Row;

			if (caret.Focused && caretIsOnThisRow)
			{
				DrawCaret(canvas, slice, caret.Visual.Column, caret.MovedAt);
			}
		}

		// WARNING: Reserved from the document as it stands after this frame's edits, not from the
		// extent captured before them. Reserving from the pre-edit extent left the clipper walking
		// one line past what Dummy had reserved the moment Enter grew the document, and EndChild
		// asserts on that. This Dummy is the only thing allowed to grow ContentSize (see
		// AddInteractiveSurface).
		void ReserveDocumentExtent(const ImVec2& origin, float contentWidth, float documentHeight)
		{
			ImGui::Dummy(ImVec2(contentWidth, documentHeight));
			ImGui::SetCursorScreenPos(origin);
		}

		// The peek's rows across the visible text, from the gutter's edge to the surface's inner edge, so the peek stays in
		// view when the text is scrolled sideways.
		ImRect PeekBlockOf(const ScreenRows& screenRows, const PeekPlacement& placement, const EditorMetrics& metrics,
						   const ImVec2& origin, float scrollX)
		{
			const float top = origin.y + static_cast<float>(screenRows.PeekTop().value_or(0)) * metrics.LineHeight;

			return ImRect(GutterRightEdge(origin, scrollX, metrics), top, ImGui::GetCurrentWindow()->InnerRect.Max.x,
						  top + static_cast<float>(placement.Rows) * metrics.LineHeight);
		}

		// WARNING: The peek is a child window of the surface, as the find bar is, so it takes the mouse from the surface.
		// BeginChild sizes its item like any other and would push the surface's CursorMaxPos out to the peek's far
		// corner; ReserveDocumentExtent has already reserved its rows, so the extent is restored after EndChild.
		std::optional<PeekAction> DrawPeekBlock(const LinePeekContent& content, std::size_t selected, const ImRect& block,
												const ResolvedScheme& scheme, bool justOpened)
		{
			ImGuiWindow* const surface = ImGui::GetCurrentWindow();
			const ImVec2 extentBeforePeek = surface->DC.CursorMaxPos;

			const float inset = Core::Scaled(kPeekInset);
			const float gap = Core::Scaled(kPeekGap);
			const ImVec2 size(std::max(0.0f, block.GetWidth() - inset * 2.0f),
							  std::max(0.0f, block.GetHeight() - gap * 2.0f));

			ImGui::SetCursorScreenPos(ImVec2(block.Min.x + inset, block.Min.y + gap));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
								ImVec2(Core::Scaled(kPeekPaddingX), Core::Scaled(kPeekPaddingY)));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));

			const bool peekVisible =
				ImGui::BeginChild("##LinePeek", size, ImGuiChildFlags_Borders,
								  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

			ImGui::PopStyleColor();
			ImGui::PopStyleVar();

			std::optional<PeekAction> answer;
			bool peekFocused = false;

			if (peekVisible)
			{
				answer = DrawLinePeek(content, selected, ImGui::GetContentRegionAvail(), scheme, justOpened);
				peekFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
			}

			ImGui::EndChild();
			surface->DC.CursorMaxPos = extentBeforePeek;

			// WARNING: A click inside the peek focuses it, and the editor would stop taking keys until its text was
			// clicked again. Focus goes back to the surface once no button is down: during a press it would end the drag
			// of a scrollbar. It comes after the content, because FocusWindow clears the active item and a button
			// released this frame would never report its click.
			if (peekFocused && !ImGui::IsAnyMouseDown())
			{
				ImGui::FocusWindow(surface);
			}

			return answer;
		}

		// The row the scroll anchor is recorded on: the first document row drawn or, when only the peek's rows were on
		// screen, the row it sits on (the last row under the last line), which is the row a view with the peek at its
		// top anchors on.
		std::optional<std::size_t> AnchorRowOf(const EditorFrameResult& result,
											   const std::optional<PeekPlacement>& placement, bool peekOnScreen,
											   const Core::RowMap& rows)
		{
			if (result.DrewRows)
			{
				return result.FirstVisibleRow;
			}

			if (!placement || !peekOnScreen || rows.GetTotalRows() == 0)
			{
				return std::nullopt;
			}

			return std::min(placement->BeforeDocumentRow, rows.GetTotalRows() - 1);
		}

		// The editor's left edge is the edge of the window that holds the surface, whose padding stands between that
		// edge and the surface. Read from inside the surface.
		float EditorLeftEdge()
		{
			const ImGuiWindow* surface = ImGui::GetCurrentWindow();

			return surface->ParentWindow != nullptr ? surface->ParentWindow->Pos.x : surface->Pos.x;
		}

		// WARNING: matches.size() stands for "none of them", the value MatchColumnsOnLine reads as no
		// current match. Zero is a real index and would leave the first occurrence unpainted.
		std::size_t IndexOfRange(const std::vector<Core::FindMatch>& matches, Core::OffsetRange range)
		{
			const auto atOrAfter = std::lower_bound(
				matches.begin(), matches.end(), range.From,
				[](const Core::FindMatch& match, std::size_t from) { return match.From < from; });

			const bool isRange = atOrAfter != matches.end() && atOrAfter->From == range.From && atOrAfter->To == range.To;

			return isRange ? static_cast<std::size_t>(std::distance(matches.begin(), atOrAfter)) : matches.size();
		}

		std::optional<BracketPair> BracketPairFor(const Core::TextBuffer& buffer,
												  const Core::OpenItem& item, SyntaxCache* syntax,
												  BracketMemo& memo)
		{
			if (syntax == nullptr)
			{
				return std::nullopt;
			}

			const std::size_t revision = buffer.GetRevision();

			if (!memo.Holds(item.Caret.Offset, revision))
			{
				memo.Fill(item.Caret.Offset, revision,
						  FindBracketPair(buffer, *syntax, item.Caret.Offset));
			}

			return memo.Pair;
		}

		void DrawVisibleRows(const Core::TextBuffer& buffer, const Core::OpenItem& item,
							 const EditorMetrics& metrics, const ImVec2& origin, float scrollX,
							 float available, bool wordWrap, bool focused, SyntaxCache* syntax,
							 const ResolvedScheme& scheme, const ScreenRows& screenRows,
							 const LensModel* lensModel, const SemanticOverlay* semanticOverlay,
							 const DiagnosticOverlay* diagnosticOverlay, bool diagnosticsStale,
							 const ChangeMarkers* changes, const ChangeMarkers* scrollbarChanges,
							 const SelectionOccurrences& occurrences, const LineAuthorView& author,
							 EditorWidgetState& widget, const HoverFrame& hoverFrame,
							 const PointerState& pointer, EditorFrameResult& result)
		{
			RowCanvas canvas;

			canvas.Draw = ImGui::GetWindowDrawList();
			canvas.Origin = origin;
			canvas.EditorLeft = EditorLeftEdge();
			canvas.ScrollX = scrollX;
			canvas.TextOriginX = origin.x + metrics.Gutter.TotalWidth;
			canvas.ContentWidth = ContentWidthOf(item, metrics, available, wordWrap);
			canvas.WindowWidth = ImGui::GetWindowWidth();
			canvas.Metrics = metrics;
			canvas.Palette = ReadEditorPalette(scheme);
			canvas.SyntaxColours = ReadSyntaxPalette(scheme);

			canvas.Draw->AddRectFilled(
				canvas.Origin,
				ImVec2(canvas.Origin.x + std::max(canvas.ContentWidth, canvas.WindowWidth),
					   canvas.Origin.y + std::max(DocumentHeightOf(item, metrics, screenRows),
												  ImGui::GetWindowHeight())),
				canvas.Palette.Background);

			CaretPaint caret;

			caret.Line = buffer.ToPosition(item.Caret.Offset).Line;
			caret.Visual = Core::CaretToVisual(buffer, item.Rows, item.Caret.Offset, item.TabWidth);
			caret.Focused = focused;
			caret.MovedAt = widget.CaretMovedAt;

			result.CaretScreenX =
				canvas.TextOriginX + static_cast<float>(caret.Visual.Column) * metrics.CellWidth;
			result.CaretScreenY =
				origin.y +
				static_cast<float>(screenRows.ScreenRowOf(caret.Visual.Row)) * metrics.LineHeight;
			result.CaretLineHeight = metrics.LineHeight;

			const ImRect surface = ImGui::GetCurrentWindow()->InnerRect;
			result.SurfaceLeft = surface.Min.x;
			result.SurfaceTop = surface.Min.y;
			result.SurfaceRight = surface.Max.x;
			result.SurfaceBottom = surface.Max.y;
			result.Viewport = ImGui::GetWindowViewport()->ID;

			const HoverIdentifier hover = ComputeHoverIdentifier(
				buffer, item, metrics, origin, canvas.TextOriginX, screenRows, syntax);
			const std::optional<std::size_t> hoveredLens =
				HoveredLensRow(pointer, screenRows, lensModel, metrics, origin, canvas.TextOriginX);

			SetEditorCursor(pointer, hover.Valid || hoveredLens.has_value(), screenRows, metrics, origin,
							scrollX);

			const DiagnosticHover diagnosticHover = ComputeDiagnosticHover(
				buffer, item, metrics, origin, canvas.TextOriginX, screenRows, diagnosticOverlay);

			if (hoverFrame.State != nullptr)
			{
				RunHover(buffer, item, canvas, screenRows, syntax, diagnosticHover, hoverFrame, result);
			}
			else
			{
				DrawDiagnosticTooltip(diagnosticHover);
			}

			DrawChangeTooltip(ComputeChangeHover(item, canvas, screenRows, changes), buffer,
							  canvas.Palette, item.TabWidth);

			if (const std::optional<OverviewTrack> track = OverviewTrackOf(ImGui::GetCurrentWindow()))
			{
				DrawChangeOverview(item, canvas, screenRows, scrollbarChanges, *track);
				DrawOccurrenceOverview(item, canvas, screenRows, occurrences, *track);
				DrawCaretOverview(canvas, screenRows.ScreenRowOf(caret.Visual.Row), *track);
			}

			LineCacheInput lineInput{buffer, item.Find.Matches, occurrences.Matches};

			lineInput.Selection = Core::SelectionRange(item.Caret);
			lineInput.CurrentMatch = item.Find.CurrentMatch;
			lineInput.SelectedOccurrence = IndexOfRange(occurrences.Matches, lineInput.Selection);
			lineInput.TabWidth = item.TabWidth;
			lineInput.Brackets = BracketPairFor(buffer, item, syntax, widget.Brackets);

			ReserveDocumentExtent(origin, canvas.ContentWidth,
								  DocumentHeightOf(item, metrics, screenRows));

			VisibleLineCache cache;

			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(screenRows.TotalScreenRows(item.Rows.GetTotalRows())),
						  metrics.LineHeight);

			bool anyDrawn = false;

			while (clipper.Step())
			{
				for (int screenRowIndex = clipper.DisplayStart; screenRowIndex < clipper.DisplayEnd;
					 ++screenRowIndex)
				{
					const ScreenRows::Row screenRow =
						screenRows.At(static_cast<std::size_t>(screenRowIndex));
					const float y = origin.y + static_cast<float>(screenRowIndex) * metrics.LineHeight;

					// The peek is drawn once after the rows, from its own first row; its rows here only keep the
					// gutter beside it empty.
					if (screenRow.Kind == ScreenRows::RowKind::Peek)
					{
						DrawGutterFill(canvas, y);
						continue;
					}

					if (screenRow.Kind == ScreenRows::RowKind::Lens)
					{
						const Lens* const lens =
							lensModel != nullptr ? lensModel->LensAt(screenRow.DocumentRow) : nullptr;

						const std::string_view waiting =
							lensModel != nullptr ? std::string_view{lensModel->GetWaitingText()} : std::string_view{};

						DrawLensRow(canvas, lens, waiting, y,
									SelectionPassesThroughLens(buffer, item, screenRow.DocumentRow),
									hoveredLens == screenRow.DocumentRow);

						continue;
					}

					const std::size_t rowIndex = screenRow.DocumentRow;
					const Core::RowRef rowRef = item.Rows.RowAt(rowIndex);

					// WARNING: Widened rather than assigned, because the clipper steps more than
					// once.
					result.FirstVisibleRow =
						anyDrawn ? std::min(result.FirstVisibleRow, rowIndex) : rowIndex;
					result.LastVisibleRow =
						anyDrawn ? std::max(result.LastVisibleRow, rowIndex) : rowIndex;
					anyDrawn = true;
					result.DrewRows = true;

					CacheLine(cache, lineInput, rowRef.Line);

					// WARNING: TokensOf caches one line at a time and returns a reference into that
					// cache, so it is fetched per row and only held across the rows of this line.
					// Hoisting it above the loop would leave the reference dangling.
					const std::vector<Token>& tokens =
						syntax != nullptr ? syntax->TokensOf(buffer, rowRef.Line)
										  : Core::SharedEmptyVector<Token>();

					const RowSlice slice = SliceRow(cache, item.Rows, rowRef, rowIndex, y);

					const std::vector<SemanticSpan>& semantic =
						semanticOverlay != nullptr ? semanticOverlay->SpansOn(rowRef.Line)
												   : Core::SharedEmptyVector<SemanticSpan>();

					const std::vector<DiagnosticSpan>& diagnostics =
						diagnosticOverlay != nullptr ? diagnosticOverlay->SpansOn(rowRef.Line)
													 : Core::SharedEmptyVector<DiagnosticSpan>();

					DrawRow(canvas, item, cache, slice, tokens, semantic, diagnostics,
							diagnosticsStale, changes, caret, hover, author, widget);
				}
			}

			clipper.End();
		}
	}

	std::size_t WrappedColumns(const WrapGeometry& geometry, std::size_t lineCount, VerticalScrollbar scrollbar)
	{
		if (geometry.CellWidth <= 0.0f)
		{
			return Core::RowMap::kNoWrap;
		}

		const float roomOfHiddenScrollbar = scrollbar == VerticalScrollbar::Hidden ? geometry.ScrollbarWidth : 0.0f;
		const float rowWidth = geometry.WidthBesideScrollbar + roomOfHiddenScrollbar;
		const LineNumberColumn numbers{.Digits = LineNumberDigits(lineCount),
									   .CellWidth = geometry.CellWidth,
									   .Gap = geometry.LineNumberGap};
		const float columnsLeftForText = (rowWidth - geometry.FixedGutterWidth - numbers.Width()) / geometry.CellWidth;

		return static_cast<std::size_t>(std::max(kLeastWrapColumns, columnsLeftForText));
	}

	Core::FrameInput RouteFocusedEditorStrokes(const CommandContext& context,
											   const Core::FrameInput& input, bool& ranRepeat)
	{
		Core::FrameInput remaining;

		const bool routable = IsFullyRoutable(context);

		for (const Core::KeyStroke& stroke : input.GetStrokes())
		{
			const Core::KeyPressedEvent pressed{stroke.Key, stroke.Modifiers, stroke.IsRepeat};

			const bool ran = routable &&
							 context.Router->OnKeyPressed(*context.Commands, *context.Keys,
														  Core::ShortcutScope::FocusedEditor,
														  pressed);

			if (!ran)
			{
				remaining.PushKey(stroke.Key, stroke.Modifiers, stroke.IsRepeat);
			}
			else if (stroke.IsRepeat)
			{
				ranRepeat = true;
			}
		}

		for (const unsigned int codepoint : input.GetCharacters())
		{
			remaining.PushCharacter(codepoint);
		}

		return remaining;
	}

	EditorFrameResult DrawEditor(const CommandContext& context, Core::OpenItem& item,
								 const EditorView& view, FindBar& findBar)
	{
		EditorFrameResult result;

		EditorWidgetState noState;
		EditorWidgetState& widget = view.Widget != nullptr ? *view.Widget : noState;

		// Last frame's rectangle is the one the mouse can be over now. The row that draws the author this frame sets
		// it again.
		const std::optional<ImRect> lastAuthorRect = std::exchange(widget.AuthorTextRect, std::nullopt);

		if (!item.Document)
		{
			result.Wrap = WrapGeometryBeforeTheText(view);
			result.RetryPressed = DrawWithoutDocumentAndReportRetry(item);
			return result;
		}

		ImGui::PushFont(Core::ImGuiRenderer::GetMonoFont(), view.FontSize);

		const Core::TextBuffer& buffer = item.Document->GetBuffer();
		const std::size_t caretAtFrameStart = item.Caret.Offset;
		const std::size_t caretLineAtFrameStart = buffer.ToPosition(caretAtFrameStart).Line;
		const EditorMetrics metrics = MeasureEditor(buffer, view.ChangeDisplay.Gutter);

		const bool documentHoldsFocus =
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		// One Escape closes one thing: whatever took it in front of the editor, then the peek, and the find bar only once
		// neither is left.
		const bool peekTakesEscape = view.Peek.has_value() && !view.EscapeTaken && documentHoldsFocus &&
									 ImGui::IsKeyPressed(ImGuiKey_Escape);
		const bool escapeTakenBeforeFind = view.EscapeTaken || peekTakesEscape;

		if (peekTakesEscape)
		{
			result.PeekAnswer = PeekAction{.Action = PeekAction::Kind::Close};
		}

		const ImVec2 fillAvailableSpace(0.0f, 0.0f);

		if (ImGui::BeginChild("##EditorSurface", fillAvailableSpace, ImGuiChildFlags_None,
							  EditorSurfaceFlags()))
		{
			// WARNING: Selecting a tab focuses the document window, not the surface inside it,
			// and only the surface draws a caret or reads keys. The flags count children: the find
			// bar is a child of this surface, and without them focus was taken back from its field
			// on every frame, which reset ImGui's NavId.
			if (documentHoldsFocus &&
				!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
			{
				ImGui::SetWindowFocus();
			}

			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const float scrollX = ImGui::GetScrollX();
			const float available = ImGui::GetContentRegionAvail().x;

			if (item.TabWidth != view.Indent.Width && view.Indent.Width > 0)
			{
				item.TabWidth = view.Indent.Width;
				Core::RecomputeLineCaches(item);
				item.RowAimCaretOffset = Core::NoRowAim;
			}

			// WARNING: Called every frame, not only when the tab width changes. Tying it to the
			// RecomputeLineCaches edge would desync the cache's width for good if view.Syntax were
			// null on the one frame the width changed.
			if (view.Syntax != nullptr)
			{
				view.Syntax->SetTabWidth(item.TabWidth);
			}

			const LensModel* const drawnLenses = view.ShowLensRows ? view.Lens : nullptr;

			const WrapGeometry wrap = WrapGeometryOf(metrics.Gutter, metrics.CellWidth, SurfaceWidthWithoutScrollbar());
			result.Wrap = wrap;

			std::optional<PeekPlacement> peekPlacement;

			if (view.Peek)
			{
				peekPlacement = PlacePeekUnder(item.Rows, view.Peek->Line);
			}

			ScreenRows screenRows{LensRowsOf(drawnLenses), peekPlacement};

			const VerticalScrollbar scrollbar = ScrollbarOfSurface(item, metrics, screenRows);
			const std::size_t columnsPerRow =
				view.WordWrap ? WrappedColumns(wrap, buffer.GetLineCount(), scrollbar) : Core::RowMap::kNoWrap;

			RewrapIfGeometryChanged(item, buffer, columnsPerRow);

			// WARNING: Placed again because the rewrap can move the rows under the peek's line. The placement above
			// only gave the scrollbar the peek's height, which the wrap does not change.
			if (peekPlacement)
			{
				peekPlacement = PlacePeekUnder(item.Rows, view.Peek->Line);
				screenRows = ScreenRows{LensRowsOf(drawnLenses), peekPlacement};
			}

			const float rowAreaWidth = RowAreaWidth(available, scrollbar);

			const PointerState pointer = AddInteractiveSurface(
				origin, ContentWidthOf(item, metrics, rowAreaWidth, view.WordWrap),
				DocumentHeightOf(item, metrics, screenRows));

			const AuthorCardInput cardInput =
				ReadAuthorCardInput(pointer, lastAuthorRect, widget.AuthorCardWindow,
									buffer.ToPosition(item.Caret.Offset).Line, context.Input);

			widget.AuthorCard.Update(cardInput, view.Author.HoverDelaySeconds);

			// WARNING: A press on the author is the card's. The caret stays, and the surface lets go of the press, or a
			// drag from it would extend a selection to wherever the mouse went.
			if (cardInput.ClickedText)
			{
				ImGui::ClearActiveID();
			}
			else
			{
				ApplyMouseToCaret(pointer, buffer, item, metrics, origin,
								  origin.x + metrics.Gutter.TotalWidth, screenRows, drawnLenses, result);
			}

			RouteMouseWheel(context);

			const bool hasMenu = context.ContextMenu != nullptr && context.Commands != nullptr &&
								 context.Keys != nullptr;

			if (result.ContextMenuOffset && hasMenu)
			{
				// WARNING: A right press moves no keyboard focus by itself, so the menu of one side
				// of a split would act on the other side's document. This comes before OpenPopup so
				// the popup records this surface as the window to hand focus back to.
				ImGui::SetWindowFocus();
				ImGui::OpenPopup("##EditorContextMenu");

				widget.ContextMenuOffset = result.ContextMenuOffset;
			}

			if (hasMenu)
			{
				Core::PushInterfaceFont();

				if (ImGui::BeginPopup("##EditorContextMenu", ImGuiWindowFlags_NoMove))
				{
					Core::RenderMenuItems(*context.Commands, *context.Keys, *context.ContextMenu);
					ImGui::EndPopup();
				}
				else
				{
					// Nothing runs from a closed menu, so nothing may act on the line it was opened on.
					widget.ContextMenuOffset.reset();
				}

				ImGui::PopFont();
			}

			// WARNING: IsWindowFocused() without flags checks the current window, the child
			// pushed by BeginChild, so only the document clicked into takes keys. It is computed
			// once and shared with the caret drawing so the keyboard and the blinking caret can
			// never disagree about which editor is active.
			const bool focused = ImGui::IsWindowFocused();

			if (focused)
			{
				const int frame = ImGui::GetFrameCount();

				if (CaretBlinkRestarts(widget.LastFocusedFrame, frame))
				{
					widget.CaretMovedAt = ImGui::GetTime();
				}

				widget.LastFocusedFrame = frame;
			}

			const EditSinks sinks{.Lens = view.Lens, .Declarations = view.Declarations};
			Core::FrameInput remaining;
			bool ranRepeat = false;

			if (focused && context.Input != nullptr)
			{
				remaining = RouteFocusedEditorStrokes(context, *context.Input, ranRepeat);
			}

			ApplyPendingActions(widget.PendingActions, item, sinks, widget.History, ranRepeat, result);

			if (focused && context.Input != nullptr)
			{
				ApplyKeyboard(remaining, buffer, item, view.Indent, sinks, RowsPerPage(metrics),
							  caretAtFrameStart, result);
			}

			// WARNING: The peek was placed against the rows as they stood before this frame's edits, and an edit that
			// removed rows would leave it past their end, where the rows below it would be read off the end of the
			// map. An edit closes the peek, so it is dropped for the rest of the frame rather than placed again.
			if (result.Edited && peekPlacement)
			{
				peekPlacement.reset();
				screenRows = ScreenRows{LensRowsOf(drawnLenses), std::nullopt};
			}

			if (item.Find.Open)
			{
				RefreshFindMatchesIfStale(item, buffer);
			}

			const SelectionOccurrences& occurrences =
				OccurrencesToShow(widget.Occurrences, buffer, item, view.HighlightOccurrences);

			// WARNING: The anchor is applied before the caret, deliberately. Both want the scroll
			// this frame and the caret is the fresher claim: a reply arriving the frame somebody
			// pressed a key must not drag the view back off where they just went.
			float scrollY = ImGui::GetScrollY();

			if (view.Anchor != nullptr && view.Anchor->Taken)
			{
				const std::size_t screenRowNow = screenRows.ScreenRowOf(
					AnchoredRow(*view.Anchor, item.Rows.FirstRowOf(view.Anchor->Line),
								item.Rows.RowsOf(view.Anchor->Line)));

				if (const std::optional<float> scroll =
						ScrollKeepingAnchor(*view.Anchor, screenRowNow, metrics.LineHeight))
				{
					ImGui::SetScrollY(*scroll);
					scrollY = *scroll;
				}
			}

			if (item.Caret.Offset != caretAtFrameStart || item.PendingScroll)
			{
				ScrollToCaret(buffer, item, metrics, view.WordWrap, screenRows, scrollY);
				widget.CaretMovedAt = ImGui::GetTime();
				item.PendingScroll = false;
			}

			if (peekPlacement && view.Peek->JustOpened)
			{
				ScrollPeekIntoView(item, metrics, screenRows, *peekPlacement, view.Peek->Line, scrollY);
			}

			const HoverFrame hoverFrame{.State = view.Hover,
										.Settings = view.HoverConfig,
										.Input = context.Input,
										.PreviewOverlay = view.HoverPreviewOverlay,
										.Semantics = view.HoverSemantics,
										.LoadingText = view.HoverLoadingText};

			const ResolvedScheme& scheme = view.Scheme != nullptr ? *view.Scheme : DarkScheme();
			const ChangeMarkers* const gutterChanges =
				view.ChangeDisplay.Gutter ? view.Changes : nullptr;
			const ChangeMarkers* const scrollbarChanges =
				view.ChangeDisplay.Scrollbar ? view.Changes : nullptr;

			// WARNING: An edit made this frame has not reached the caller's quiet period yet, which starts from
			// the result. Drawn now, the text would jump right with the keystroke for one frame and then vanish.
			// Nor has a caret moved to another line this frame reached the rest the caller waits for.
			const bool leftLineThisFrame = buffer.ToPosition(item.Caret.Offset).Line != caretLineAtFrameStart;

			LineAuthorView author = view.Author;
			author.Hidden = author.Hidden || result.Edited || (author.WaitsForRest && leftLineThisFrame);

			DrawVisibleRows(buffer, item, metrics, origin, scrollX, rowAreaWidth, view.WordWrap,
							focused, view.Syntax, scheme, screenRows, drawnLenses, view.Semantic,
							view.Diagnostics, view.DiagnosticsStale, gutterChanges,
							scrollbarChanges, occurrences, author, widget, hoverFrame, pointer, result);

			if (cardInput.OverText)
			{
				ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
			}

			RunAuthorCard(author, buffer.ToPosition(item.Caret.Offset).Line, widget, result);

			std::optional<ImRect> peekBlock;

			if (peekPlacement)
			{
				peekBlock = PeekBlockOf(screenRows, *peekPlacement, metrics, origin, scrollX);
			}

			const bool peekOnScreen =
				peekBlock.has_value() && peekBlock->Overlaps(ImGui::GetCurrentWindow()->InnerClipRect);

			// WARNING: A frame that drew nothing reports row zero, and adopting it would claim
			// the reader had scrolled to the top. The anchor is recorded as a line and a sub-row
			// because the row index is a position in a mapping that rewrapping rebuilds. A view
			// scrolled onto the peek's rows alone draws no document row, and keeping the anchor of
			// some earlier frame would scroll back there the next time a row above changed.
			if (view.Anchor != nullptr)
			{
				if (const std::optional<std::size_t> anchorRow =
						AnchorRowOf(result, peekPlacement, peekOnScreen, item.Rows))
				{
					const Core::RowRef topRow = item.Rows.RowAt(*anchorRow);

					view.Anchor->Line = topRow.Line;
					view.Anchor->SubRow = topRow.SubRow;
					view.Anchor->ScreenRow = screenRows.ScreenRowOf(*anchorRow);
					view.Anchor->ScrollY = ImGui::GetScrollY();
					view.Anchor->Taken = true;
				}
			}

			// The peek is drawn after the rows for the find bar's reason below, and before the bar so the bar stays
			// on top of it.
			if (peekOnScreen && view.Peek->Content != nullptr)
			{
				const std::optional<PeekAction> answer =
					DrawPeekBlock(*view.Peek->Content, view.Peek->Selected, *peekBlock, scheme, view.Peek->JustOpened);

				if (answer && !result.PeekAnswer)
				{
					result.PeekAnswer = answer;
				}
			}

			// WARNING: The find bar is drawn after the rows, so a click on it is a click on it:
			// the surface's own hit test has already run against a rectangle the bar now sits on
			// top of, and a nested child window takes the hover from its parent.
			if (item.Find.Open)
			{
				findBar.Draw(item, buffer, documentHoldsFocus, escapeTakenBeforeFind);
			}
		}

		ImGui::EndChild();
		ImGui::PopFont();

		return result;
	}

}
