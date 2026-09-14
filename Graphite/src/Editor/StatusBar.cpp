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

	void DrawStatusBar(const Core::DocumentSet& documents,
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
			return;
		}

		const auto active = documents.GetActive();
		const bool hasDocument = active && documents.At(*active).Document.has_value();

		if (hasDocument)
		{
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
	}
}