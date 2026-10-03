#include "Editor/StatusBar.h"

#include "Core/Fs/FileSystem.h"
#include "Core/Text/Caret.h"
#include "Core/Text/Columns.h"
#include "Core/Text/DocumentSet.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace Graphite {

	namespace {
		const char* LineEndingName(Core::LineEnding ending, bool mixed)
		{
			if (ending == Core::LineEnding::Crlf)
				return mixed ? "CRLF (mixed)" : "CRLF";

			return mixed ? "LF (mixed)" : "LF";
		}

		std::string RelativePath(const std::filesystem::path& path,
								 const std::filesystem::path& root)
		{
			const auto relative = Core::RelativeTo(path, root);

			return Core::ToUtf8(relative ? *relative : path);
		}

		float CentredTextY(float barHeight)
		{
			// The bar is one frame tall — FontSize + 2 * FramePadding.y — and a line of text
			// is FontSize tall, both measured from the window's own top edge.
			return (barHeight - ImGui::GetTextLineHeight()) * 0.5f;
		}

		// Positioned outright rather than through SameLine, as DrawCentred beside it already is.
		// SameLine places relative to the item before it, so it silently needs one - and the
		// server's state is drawn on frames where no document is open and nothing precedes it.
		void DrawRightAligned(const std::string& text, float barHeight)
		{
			const float width = ImGui::CalcTextSize(text.c_str()).x;

			ImGui::SetCursorPos(
				ImVec2(ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x,
					   CentredTextY(barHeight)));
			ImGui::TextUnformatted(text.c_str());
		}

		void DrawCentred(const std::string& text, float barHeight)
		{
			const float width = ImGui::CalcTextSize(text.c_str()).x;

			ImGui::SetCursorPos(
				ImVec2((ImGui::GetWindowWidth() - width) * 0.5f, CentredTextY(barHeight)));
			ImGui::TextUnformatted(text.c_str());
		}

		// One piece of the git segment. Clickable pieces show a hand and take a click; the
		// greyed "No repository" is inert.
		//
		// A clickable piece is an invisible button with the text drawn over it, not a text item
		// asked IsItemClicked. A text item has no id, so a press on it is a press on the bar's
		// empty space: at the end of that frame ImGui focuses the bar itself, as it does for any
		// click on a window's background. That lands after the panel revealed by the click took
		// focus in the same frame, and the dock node then selects the tab of whatever window has
		// focus - the bar - so the panel stayed buried. A button owns the press, so the bar is
		// never refocused, and it reports the click on release, after the press has settled.
		// Not a Selectable: its hover highlight would fill the bar's height around one word.
		bool DrawGitPiece(const char* id, const std::string& text, bool clickable,
						  const std::string& tooltip)
		{
			const ImVec2 size = ImGui::CalcTextSize(text.c_str());

			// InvisibleButton refuses a zero size, so an empty piece is drawn as inert text.
			if (!clickable || size.x <= 0.0f)
			{
				ImGui::TextDisabled("%s", text.c_str());

				if (ImGui::IsItemHovered() && !tooltip.empty())
					ImGui::SetTooltip("%s", tooltip.c_str());

				return false;
			}

			// Sized exactly as TextUnformatted sizes the same string, so the pieces after it
			// land where they did when this was a text item.
			const bool clicked = ImGui::InvisibleButton(id, size);

			ImGui::GetWindowDrawList()->AddText(ImGui::GetItemRectMin(),
												ImGui::GetColorU32(ImGuiCol_Text), text.c_str());

			if (!ImGui::IsItemHovered())
				return clicked;

			if (!tooltip.empty())
				ImGui::SetTooltip("%s", tooltip.c_str());

			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			return clicked;
		}

		// Laid out left to right from the bar's own left edge, each piece a separate item so
		// each has its own hover and click. Returns the piece clicked, if any.
		StatusBarClick DrawGitSegment(const GitStatusSegment& segment, float barHeight)
		{
			StatusBarClick clicked = StatusBarClick::None;

			ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, CentredTextY(barHeight)));

			if (DrawGitPiece("##GitBranch", segment.Branch, segment.HasRepository, segment.BranchTooltip))
				clicked = StatusBarClick::GitBranch;

			if (!segment.Distance.empty())
			{
				ImGui::SameLine();
				ImGui::TextUnformatted(segment.Distance.c_str());
			}

			if (!segment.Changes.empty())
			{
				ImGui::SameLine(0.0f, ImGui::GetFontSize());

				if (DrawGitPiece("##GitChanges", segment.Changes, true, {}))
					clicked = StatusBarClick::GitChanges;
			}

			return clicked;
		}

		std::string DescribeDocument(const Core::OpenItem& item, const std::filesystem::path& root)
		{
			std::string description = RelativePath(item.Path, root);

			if (item.Document->IsModified())
				description = "* " + description;

			if (item.Document->IsReadOnly())
				description += "  (read-only: not valid text for its encoding)";

			return description;
		}

		std::string DescribeCaret(const Core::OpenItem& item)
		{
			const Core::TextBuffer& buffer = item.Document->GetBuffer();
			const Core::Position where = buffer.ToPosition(item.Caret.Offset);
			const std::size_t column = Core::ColumnOfByte(buffer.LineText(where.Line), where.Column,
														  item.TabWidth);

			std::string description =
				"Ln " + std::to_string(where.Line + 1) + ", Col " + std::to_string(column + 1);

			const auto [from, to] = Core::SelectionRange(item.Caret);

			if (from != to)
				description += " (" + std::to_string(to - from) + " selected)";

			return description;
		}

		std::string DescribeTextFormat(const Core::OpenItem& item)
		{
			const std::string encoding{Core::EncodingName(item.Document->GetFileEncoding())};

			return encoding + "    " +
				   LineEndingName(item.Document->GetDocumentLineEnding(), item.Document->WasMixed());
		}
	}

	StatusBarClick DrawStatusBar(const Core::DocumentSet& documents,
								 const std::filesystem::path& root,
								 const StatusMessages& messages)
	{
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float height = ImGui::GetFrameHeight();

		constexpr ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

		if (!ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down, height, flags))
		{
			ImGui::End();
			return StatusBarClick::None;
		}

		const auto active = documents.GetActive();
		const bool hasDocument = active && documents.At(*active).Document.has_value();

		StatusBarClick clicked = StatusBarClick::None;

		if (messages.Git)
		{
			clicked = DrawGitSegment(*messages.Git, height);
		}

		if (hasDocument)
		{
			// Beside the git segment when there is one, a font-size gap apart; at the bar's
			// left edge otherwise, where it has always been.
			if (messages.Git)
				ImGui::SameLine(0.0f, ImGui::GetFontSize() * 2.0f);

			ImGui::SetCursorPosY(CentredTextY(height));
			ImGui::TextUnformatted(DescribeDocument(documents.At(*active), root).c_str());
		}

		std::string rightAligned;

		// Problems first, ahead of the server phrase: a count of what is wrong is worth seeing
		// before a description of what is still indexing. Absent whenever there is nothing to
		// report — see the field's own comment on StatusMessages.
		if (messages.Problems && !messages.Problems->empty())
		{
			rightAligned = *messages.Problems;
		}

		// Server next, and only when there is something to say — a language with no server
		// configured contributes nothing to the bar, not even a separator.
		//
		// Outside the has-a-document branch deliberately: a server is started when the folder
		// opens, before anything has been opened from it, so the frames where its indexing most
		// wants reporting are exactly the ones with no document to hang the report off.
		if (messages.Server && !messages.Server->empty())
		{
			if (!rightAligned.empty())
			{
				rightAligned += "    ";
			}

			rightAligned += *messages.Server;
		}

		// Last of the three, so it sits furthest from the caret and encoding figures the eye goes
		// to. It is the slowest-moving thing in the bar and the one nobody needs to find quickly.
		if (messages.Background && !messages.Background->empty())
		{
			if (!rightAligned.empty())
			{
				rightAligned += "    ";
			}

			rightAligned += *messages.Background;
		}

		if (hasDocument)
		{
			const Core::OpenItem& item = documents.At(*active);

			if (!rightAligned.empty())
			{
				rightAligned += "    ";
			}

			rightAligned += DescribeCaret(item) + "    " + DescribeTextFormat(item);
		}

		if (!rightAligned.empty())
		{
			DrawRightAligned(rightAligned, height);

			if (ImGui::IsItemHovered())
			{
				std::string tooltip;

				if (messages.ServerDetail && !messages.ServerDetail->empty())
				{
					tooltip = *messages.ServerDetail;
				}

				if (messages.BackgroundDetail && !messages.BackgroundDetail->empty())
				{
					if (!tooltip.empty())
					{
						tooltip += "\n\n";
					}

					tooltip += *messages.BackgroundDetail;
				}

				if (!tooltip.empty())
				{
					ImGui::SetTooltip("%s", tooltip.c_str());
				}
			}
		}

		// A transient message outranks a running search: it answers something the user just did
		// and is gone in a few seconds, so losing it loses the only confirmation there was.
		// Search progress is regenerated every frame, already appears in the panel's own summary
		// line, and comes right back once the transient message clears.
		const std::optional<std::string>& centred = messages.Transient ? messages.Transient : messages.Search;

		if (centred && !centred->empty())
		{
			DrawCentred(*centred, height);
		}

		ImGui::End();

		return clicked;
	}
}