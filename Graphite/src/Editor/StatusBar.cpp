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
	}

	void DrawStatusBar(const Core::DocumentSet& documents,
					   const std::filesystem::path& root,
					   const std::optional<std::string>& transientMessage)
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
			const Core::TextBuffer& buffer = item.Document->GetBuffer();

			// The strip is exactly one frame tall: height == FontSize + 2 * FramePadding.y
			// (that is what GetFrameHeight() above returns), and a line of text is
			// FontSize tall (GetTextLineHeight()). Centering it means leaving
			// (height - textHeight) / 2 == FramePadding.y of empty space above the text,
			// which is what SetCursorPosY places the cursor at — measured from the
			// window's own top edge, the same origin GetCursorPosY() uses, so this holds
			// regardless of WindowPadding and stays centred if the font size changes at
			// runtime. Both sides of the strip share one cursor row, so SameLine() below
			// carries this same Y to the right-hand group.
			const float textHeight = ImGui::GetTextLineHeight();
			ImGui::SetCursorPosY((height - textHeight) * 0.5f);

			// A leading marker rather than a colour: the strip has no guarantee its text
			// colour reads as "warning" against every theme, and a dirty document is not
			// an error worth one.
			std::string left = RelativePath(item.Path, root);

			if (item.Document->IsModified())
				left = "* " + left;

			// Invalid UTF-8 for its own declared encoding - the one thing that makes
			// editing refuse to touch a file - is exactly what the status bar exists to
			// surface, not silently allow.
			if (item.Document->IsReadOnly())
				left += "  (read-only: not valid text for its encoding)";

			ImGui::TextUnformatted(left.c_str());

			const Core::Position where = buffer.ToPosition(item.Caret.Offset);
			const std::size_t column = Core::ColumnOfByte(
				buffer.LineText(where.Line), where.Column, Core::DefaultTabWidth);

			// One-based, because that is how people and every compiler count.
			std::string position = "Ln " + std::to_string(where.Line + 1) + ", Col " + std::to_string(column + 1);

			const auto [from, to] = Core::SelectionRange(item.Caret);

			if (from != to)
				position += " (" + std::to_string(to - from) + " selected)";

			const std::string encoding{Core::EncodingName(item.Document->GetFileEncoding())};
			const std::string ending =
				LineEndingName(item.Document->GetDocumentLineEnding(), item.Document->WasMixed());

			const std::string right = position + "    " + encoding + "    " + ending;
			const float width = ImGui::CalcTextSize(right.c_str()).x;

			ImGui::SameLine(ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x);
			ImGui::TextUnformatted(right.c_str());
		}

		if (transientMessage && !transientMessage->empty())
		{
			// Centred using the same math as the row above: the strip is exactly one
			// frame tall, and (height - textHeight) / 2 is where that leaves for the text
			// to sit, this time measured from the window's own top edge again rather than
			// carried over from the row above, since there may have been no active
			// document to draw one.
			const float textHeight = ImGui::GetTextLineHeight();
			const float messageWidth = ImGui::CalcTextSize(transientMessage->c_str()).x;

			ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - messageWidth) * 0.5f,
									   (height - textHeight) * 0.5f));
			ImGui::TextUnformatted(transientMessage->c_str());
		}

		ImGui::End();
	}
}