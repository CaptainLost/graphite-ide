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

		void DrawRightAligned(const std::string& text)
		{
			const float width = ImGui::CalcTextSize(text.c_str()).x;

			ImGui::SameLine(ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x);
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

		if (active && documents.At(*active).Document)
		{
			const Core::OpenItem& item = documents.At(*active);

			ImGui::SetCursorPosY(CentredTextY(height));
			ImGui::TextUnformatted(DescribeDocument(item, root).c_str());

			DrawRightAligned(DescribeCaret(item) + "    " + DescribeTextFormat(item));
		}

		// A running search outranks a transient message: it is the longer-lived of the two, and
		// a "Saved" toast that happens to land mid-search would otherwise blank out the count.
		const std::optional<std::string>& centred = messages.Search ? messages.Search : messages.Transient;

		if (centred && !centred->empty())
		{
			DrawCentred(*centred, height);
		}

		ImGui::End();
	}
}