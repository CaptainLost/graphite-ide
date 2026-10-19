#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Editor/TextEditorWidget.h"
#include "Editor/Find/FindBar.h"
#include "Editor/Hover/HoverState.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Overlays/LensModel.h"
#include "Git/Changes/ChangeMarkers.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/SyntaxCache.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/EditorPalette.h"
#include "Theme/Palettes.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuItem.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

	constexpr float kEditorFontSize = 20.0f;
	constexpr float kEditorLineHeight = 28.0f;
	constexpr float kTextOffset = 4.0f;
	constexpr float kDocumentWidth = 800.0f;
	constexpr float kDocumentHeight = 400.0f;
	constexpr float kIntoTheRow = 2.0f;
	constexpr float kJustInside = 1.0f;
	constexpr float kJustPast = 1.0f;
	constexpr float kScanStep = 1.0f;
	constexpr float kHalf = 0.5f;
	constexpr std::size_t kFirstRow = 0;
	constexpr std::size_t kFirstLine = 0;
	constexpr std::size_t kSecondLine = 1;
	constexpr std::size_t kReferenceCount = 3;
	constexpr const char* kLensLabel = "3 references";
	constexpr const char* kTooltipWindow = "##Tooltip_00";
	constexpr std::string_view kEditorSurface = "##EditorSurface";
	constexpr std::string_view kHoverTooltipPrefix = "##HoverTooltip";

	Core::OpenItem ItemWith(std::string content)
	{
		const std::filesystem::path path = "C:/p/widget.txt";

		Core::Result<Core::TextDocument> document = Core::TextDocument::FromContent(std::move(content), path);
		REQUIRE(document);

		Core::OpenItem item;
		item.Document = std::make_shared<Core::TextDocument>(std::move(document).GetValue());
		item.Path = path;
		Core::RecomputeLineCaches(item);

		return item;
	}

	const Core::TextBuffer& BufferOf(const Core::OpenItem& item)
	{
		return item.Document->GetBuffer();
	}

	struct EditorFrames
	{
		ImGuiFrames Frames;
		Core::OpenItem Item;
		Graphite::EditorView View;
		Graphite::CommandContext Context;
		Graphite::FindBar Bar;
		Graphite::EditorFrameResult Result;
		std::optional<std::size_t> LensClicked;
		int StackBefore = 0;
		int StackAfter = 0;

		explicit EditorFrames(std::string content)
			: Item(ItemWith(std::move(content)))
		{
			View.FontSize = kEditorFontSize;
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(kDocumentWidth, kDocumentHeight));
			ImGui::Begin("Document", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

			StackBefore = ImGui::GetCurrentContext()->FontStack.Size;
			Result = Graphite::DrawEditor(Context, Item, View, Bar);
			StackAfter = ImGui::GetCurrentContext()->FontStack.Size;

			if (Result.LensClicked)
			{
				LensClicked = Result.LensClicked;
			}

			ImGui::End();
		}

		void Next()
		{
			Frames.Next([this] { Draw(); });
		}

		void HoverAt(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Next();
		}

		void Click(ImVec2 at, ImGuiMouseButton button = ImGuiMouseButton_Left)
		{
			Frames.Click(at, [this] { Draw(); }, button);
		}

		ImVec2 CaretScreenPos() const { return ImVec2(Result.CaretScreenX, Result.CaretScreenY); }
	};

	float InterfaceWidthAt(float size, const char* text)
	{
		Core::PushInterfaceFont(size);
		const float width = ImGui::CalcTextSize(text).x;
		ImGui::PopFont();

		return width;
	}

	struct FaceSizes
	{
		float InterfaceFontSize = 0.0f;
		float InterfaceLineHeight = 0.0f;
		float CodeLineHeight = 0.0f;
	};

	FaceSizes MeasureFaces()
	{
		FaceSizes sizes;

		Core::PushInterfaceFont();
		sizes.InterfaceFontSize = ImGui::GetFontSize();
		sizes.InterfaceLineHeight = ImGui::GetTextLineHeight();
		ImGui::PopFont();

		Core::PushCodeFont(kEditorFontSize);
		sizes.CodeLineHeight = ImGui::GetTextLineHeight();
		ImGui::PopFont();

		return sizes;
	}

	const ImGuiWindow* ShownTooltip()
	{
		const ImGuiWindow* tooltip = ImGui::FindWindowByName(kTooltipWindow);
		return tooltip != nullptr && tooltip->Active ? tooltip : nullptr;
	}

	Graphite::Diagnostic ErrorAt(std::size_t line, std::size_t column, std::size_t endColumn, std::string message)
	{
		Graphite::Diagnostic diagnostic;
		diagnostic.Line = line;
		diagnostic.Column = Graphite::Utf16Column(column);
		diagnostic.EndLine = line;
		diagnostic.EndColumn = Graphite::Utf16Column(endColumn);
		diagnostic.Message = std::move(message);
		diagnostic.Source = "clangd";

		return diagnostic;
	}

	void DeclareReferencedFirstLine(Graphite::LensModel& lenses, const Core::OpenItem& item)
	{
		lenses.SetDeclarations({Graphite::Declaration{.Name = "Draw",
													  .Kind = Graphite::DeclarationKind::Function,
													  .Line = kFirstLine,
													  .Column = Graphite::Utf16Column(0)}},
							   item.Rows, BufferOf(item), item.TabWidth);
		lenses.SetCount(kFirstRow, kReferenceCount);
	}

	void PlaceCaret(Core::OpenItem& item, std::size_t offset)
	{
		item.Caret = Core::Caret{.Offset = offset, .Anchor = offset};
	}

	std::size_t CaretLine(const Core::OpenItem& item)
	{
		return BufferOf(item).ToPosition(item.Caret.Offset).Line;
	}

	Graphite::EditorPalette DarkEditorPalette()
	{
		return Graphite::ReadEditorPalette(Graphite::DarkScheme());
	}

	const ImGuiWindow* EditorSurface()
	{
		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(kEditorSurface) != std::string_view::npos)
			{
				return window;
			}
		}

		return nullptr;
	}

	const ImGuiWindow* ShownHoverTooltip()
	{
		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.starts_with(kHoverTooltipPrefix) && window->Active)
			{
				return window;
			}
		}

		return nullptr;
	}

	VerticalExtent InkOf(std::string_view text)
	{
		ImFontBaked* const baked = ImGui::GetFontBaked();
		const float scale = ImGui::GetFontSize() / baked->Size;

		VerticalExtent ink;

		for (const char character : text)
		{
			const ImFontGlyph* const glyph = baked->FindGlyph(static_cast<ImWchar>(static_cast<unsigned char>(character)));

			if (glyph == nullptr || !glyph->Visible)
			{
				continue;
			}

			ink.Top = std::min(ink.Top, glyph->Y0 * scale);
			ink.Bottom = std::max(ink.Bottom, glyph->Y1 * scale);
		}

		return ink;
	}

	std::optional<VerticalExtent> VerticalExtentInColourBetween(const ImDrawList& drawing, ImU32 colour, float left,
																 float right)
	{
		VerticalExtent extent;
		bool foundVertex = false;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour || vertex.pos.x < left || vertex.pos.x > right)
			{
				continue;
			}

			foundVertex = true;
			extent.Top = std::min(extent.Top, vertex.pos.y);
			extent.Bottom = std::max(extent.Bottom, vertex.pos.y);
		}

		if (!foundVertex)
		{
			return std::nullopt;
		}

		return extent;
	}

	struct CodeFaceMeasure
	{
		float CellWidth = 0.0f;
		VerticalExtent Ink;
	};

	CodeFaceMeasure MeasureCodeFace(std::string_view text)
	{
		CodeFaceMeasure measure;

		Core::PushCodeFont(kEditorFontSize);
		measure.CellWidth = Core::MonospaceCellWidth();
		measure.Ink = InkOf(text);
		ImGui::PopFont();

		return measure;
	}
}

TEST_CASE("A lens label is hit across the width it is drawn at")
{
	EditorFrames editor("void Draw();\n");

	Graphite::LensModel lenses;
	DeclareReferencedFirstLine(lenses, editor.Item);
	editor.View.Lens = &lenses;

	float labelWidth = 0.0f;
	float interfaceSizeWidth = 0.0f;

	editor.Frames.Next([&] {
		editor.Draw();
		labelWidth = InterfaceWidthAt(kEditorFontSize, kLensLabel);
		interfaceSizeWidth = InterfaceWidthAt(Core::kInterfaceFontSize, kLensLabel);
	});

	REQUIRE(interfaceSizeWidth < labelWidth - kJustInside);

	// WARNING: The label's left edge is read off the caret, which only works while the declaration starts in
	// column 0 and the caret sits at the start of the row below its lens.
	const float labelLeft = editor.CaretScreenPos().x;
	const float lensRowY = editor.CaretScreenPos().y - kIntoTheRow;

	editor.LensClicked.reset();
	editor.Click(ImVec2(labelLeft + labelWidth - kJustInside, lensRowY));
	CHECK(editor.LensClicked == std::optional<std::size_t>{kFirstRow});

	editor.LensClicked.reset();
	editor.Click(ImVec2(labelLeft + labelWidth + kJustPast, lensRowY));
	CHECK_FALSE(editor.LensClicked.has_value());

	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("The editor's context menu opens in the interface face")
{
	EditorFrames editor("int value = 0;\n");

	Core::CommandRegistry commands;
	Core::Keymap keys;
	const std::vector<Core::MenuItem> menu = {Core::MenuItem::Section("Edit"), Core::MenuItem::Separator()};

	editor.Context.Commands = &commands;
	editor.Context.Keys = &keys;
	editor.Context.ContextMenu = &menu;

	FaceSizes sizes;
	editor.Frames.Next([&] {
		editor.Draw();
		sizes = MeasureFaces();
	});

	const ImVec2 caret = editor.CaretScreenPos();
	editor.Click(ImVec2(caret.x + kIntoTheRow, caret.y + kIntoTheRow), ImGuiMouseButton_Right);
	editor.Next();

	const ImGuiContext& context = *ImGui::GetCurrentContext();
	REQUIRE(context.OpenPopupStack.Size > 0);

	const ImGuiWindow* popup = context.OpenPopupStack.back().Window;
	REQUIRE(popup != nullptr);

	CHECK(popup->FontRefSize == sizes.InterfaceFontSize);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("A diagnostic tooltip over the editor is set in the interface face")
{
	EditorFrames editor("int value = 0;\n");

	Graphite::DiagnosticOverlay diagnostics;
	diagnostics.Set({ErrorAt(0, 0, 9, "unused variable")}, BufferOf(editor.Item), editor.Item.TabWidth);
	editor.View.Diagnostics = &diagnostics;

	FaceSizes sizes;
	editor.Frames.Next([&] {
		editor.Draw();
		sizes = MeasureFaces();
	});

	const ImVec2 caret = editor.CaretScreenPos();
	editor.HoverAt(ImVec2(caret.x + kIntoTheRow, caret.y + kIntoTheRow));
	editor.Next();

	const ImGuiWindow* tooltip = ShownTooltip();
	REQUIRE(tooltip != nullptr);

	CHECK(tooltip->FontRefSize == sizes.InterfaceFontSize);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("The change tooltip names its hunk in the interface face and keeps the old line in the code face")
{
	constexpr std::size_t kOldCount = 3;

	EditorFrames editor("new line\nsecond\n");

	Graphite::ChangeMarkers changes;
	changes.Lines.resize(BufferOf(editor.Item).GetLineCount());
	changes.Lines[0] = Graphite::LineChange{.Kind = Graphite::ChangeKind::Modified};
	changes.Hunks = {Graphite::ChangeHunk{.Kind = Graphite::HunkKind::Modified,
										  .FirstLine = 0,
										  .LineCount = 1,
										  .MarkedCount = 1,
										  .OldCount = kOldCount,
										  .OldLines = {"old line"}}};
	editor.View.Changes = &changes;

	FaceSizes sizes;
	editor.Frames.Next([&] {
		editor.Draw();
		sizes = MeasureFaces();
	});

	const ImVec2 caret = editor.CaretScreenPos();
	const float rowMiddleY = caret.y + kEditorLineHeight * kHalf;

	for (float x = 0.0f; x < caret.x && ShownTooltip() == nullptr; x += kScanStep)
	{
		editor.HoverAt(ImVec2(x, rowMiddleY));
	}

	REQUIRE(ShownTooltip() != nullptr);

	editor.Next();
	editor.Next();

	const ImGuiWindow* tooltip = ShownTooltip();
	REQUIRE(tooltip != nullptr);

	constexpr float kInterfaceLines = 2.0f;
	constexpr float kGapsBetweenLines = 2.0f;

	const float spacing = ImGui::GetStyle().ItemSpacing.y;

	CHECK(tooltip->ContentSize.y == sizes.InterfaceLineHeight * kInterfaceLines + sizes.CodeLineHeight +
										spacing * kGapsBetweenLines);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("The editor's line height is 1.4 times its font size")
{
	constexpr float kSmallFontSize = 13.0f;
	constexpr float kSmallLineHeight = 18.0f;

	EditorFrames editor("first\nsecond\n");
	editor.View.FontSize = kSmallFontSize;

	editor.Next();
	const float firstLineTop = editor.CaretScreenPos().y;

	PlaceCaret(editor.Item, BufferOf(editor.Item).LineStart(kSecondLine));
	editor.Next();

	CHECK(editor.CaretScreenPos().y - firstLineTop == kSmallLineHeight);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("A click halfway down the second line lands on the second line")
{
	EditorFrames editor("first\nsecond\nthird\n");
	editor.Next();

	const ImVec2 firstLine = editor.CaretScreenPos();
	editor.Click(ImVec2(firstLine.x + kIntoTheRow, firstLine.y + kEditorLineHeight + kEditorLineHeight * kHalf));

	CHECK(CaretLine(editor.Item) == kSecondLine);
}

TEST_CASE("A click a pixel above the next line's top stays on the line above")
{
	constexpr float kLinesAboveTheThird = 2.0f;

	EditorFrames editor("first\nsecond\nthird\n");
	editor.Next();

	const ImVec2 firstLine = editor.CaretScreenPos();
	const float thirdLineTop = firstLine.y + kEditorLineHeight * kLinesAboveTheThird;
	editor.Click(ImVec2(firstLine.x + kIntoTheRow, thirdLineTop - kJustInside));

	CHECK(CaretLine(editor.Item) == kSecondLine);
}

TEST_CASE("The caret spans the line and its text is centred")
{
	constexpr float kCellsBeforeTheCaret = 2.0f;
	constexpr float kCellsPastTheLine = 10.0f;

	EditorFrames editor("M \n");

	CodeFaceMeasure face;
	editor.Frames.Next([&] {
		editor.Draw();
		face = MeasureCodeFace("M");
	});

	const ImVec2 lineStart = editor.CaretScreenPos();
	editor.Click(ImVec2(lineStart.x + face.CellWidth * kCellsPastTheLine, lineStart.y + kIntoTheRow));

	const float caretX = lineStart.x + face.CellWidth * kCellsBeforeTheCaret;
	REQUIRE(editor.CaretScreenPos().x == doctest::Approx(caretX));

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const float halfCell = face.CellWidth * kHalf;
	const std::optional<VerticalExtent> caret = VerticalExtentInColourBetween(
		*surface->DrawList, DarkEditorPalette().Caret, caretX - halfCell, caretX + halfCell);
	const std::optional<VerticalExtent> glyph = VerticalExtentInColourBetween(
		*surface->DrawList, DarkEditorPalette().Text, lineStart.x - halfCell, lineStart.x + face.CellWidth + halfCell);
	REQUIRE(caret.has_value());
	REQUIRE(glyph.has_value());

	CHECK(caret->Bottom - caret->Top == doctest::Approx(kEditorLineHeight));
	CHECK(caret->Top >= lineStart.y);
	CHECK(caret->Top < lineStart.y + kJustInside);

	CHECK(glyph->Top - face.Ink.Top == doctest::Approx(lineStart.y + kTextOffset));
	CHECK(glyph->Bottom - face.Ink.Bottom == doctest::Approx(lineStart.y + kTextOffset));
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("A line number is centred in its line")
{
	EditorFrames editor("M\nM");

	CodeFaceMeasure face;
	editor.Frames.Next([&] {
		editor.Draw();
		face = MeasureCodeFace("2");
	});

	const float secondLineTop = editor.CaretScreenPos().y + kEditorLineHeight;

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const std::optional<VerticalExtent> number = VerticalExtentInColour(*surface->DrawList, DarkEditorPalette().Number);
	REQUIRE(number.has_value());

	CHECK(number->Top - face.Ink.Top == doctest::Approx(secondLineTop + kTextOffset));
	CHECK(number->Bottom - face.Ink.Bottom == doctest::Approx(secondLineTop + kTextOffset));
}

TEST_CASE("A selection rectangle covers the full line height")
{
	constexpr std::size_t kSelectionEnd = 5;

	EditorFrames editor("first line\n");
	editor.Next();

	const float lineTop = editor.CaretScreenPos().y;

	editor.Item.Caret = Core::Caret{.Offset = kSelectionEnd, .Anchor = 0};
	editor.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const std::optional<VerticalExtent> selection =
		VerticalExtentInColour(*surface->DrawList, DarkEditorPalette().Selection);
	REQUIRE(selection.has_value());

	CHECK(selection->Top == lineTop);
	CHECK(selection->Bottom == lineTop + kEditorLineHeight);
}

TEST_CASE("A lens row is one line tall at the new height")
{
	EditorFrames editor("void Draw();\n");
	editor.Next();

	const float lineTopWithoutLens = editor.CaretScreenPos().y;

	Graphite::LensModel lenses;
	DeclareReferencedFirstLine(lenses, editor.Item);
	editor.View.Lens = &lenses;
	editor.Next();

	CHECK(editor.CaretScreenPos().y - lineTopWithoutLens == kEditorLineHeight);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("A lens label is centred in its row")
{
	EditorFrames editor("void Draw();\n");
	editor.Next();

	const float lensRowTop = editor.CaretScreenPos().y;

	Graphite::LensModel lenses;
	DeclareReferencedFirstLine(lenses, editor.Item);
	editor.View.Lens = &lenses;

	VerticalExtent ink;
	editor.Frames.Next([&] {
		editor.Draw();
		Core::PushInterfaceFont(kEditorFontSize);
		ink = InkOf(kLensLabel);
		ImGui::PopFont();
	});

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const std::optional<VerticalExtent> label = VerticalExtentInColour(*surface->DrawList, DarkEditorPalette().Lens);
	REQUIRE(label.has_value());

	CHECK(label->Top - ink.Top == doctest::Approx(lensRowTop + kTextOffset));
	CHECK(label->Bottom - ink.Bottom == doctest::Approx(lensRowTop + kTextOffset));
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Scrolling to the caret keeps it in view at the new height")
{
	constexpr std::size_t kLineCount = 100;
	constexpr std::size_t kCaretLine = 50;

	std::string content;

	for (std::size_t line = 0; line < kLineCount; ++line)
	{
		content += "line\n";
	}

	EditorFrames editor(std::move(content));
	editor.Next();

	PlaceCaret(editor.Item, BufferOf(editor.Item).LineStart(kCaretLine));
	editor.Item.PendingScroll = true;
	editor.Next();
	editor.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const float caretTop = editor.CaretScreenPos().y;

	CHECK(caretTop >= surface->Pos.y);
	CHECK(caretTop + kEditorLineHeight <= surface->Pos.y + surface->Size.y);
}

TEST_CASE("The hover tooltip anchors one line below the hovered token")
{
	constexpr std::size_t kTokenColumn = 4;
	constexpr std::size_t kTokenEndColumn = 9;

	EditorFrames editor("int value = 0;\n");

	Graphite::SyntaxCache syntax;
	syntax.SetSyntax(Graphite::SyntaxOf(Graphite::LanguageId::Cpp));

	Graphite::DiagnosticOverlay diagnostics;
	diagnostics.Set({ErrorAt(kFirstLine, kTokenColumn, kTokenEndColumn, "unused variable")}, BufferOf(editor.Item),
					editor.Item.TabWidth);

	Graphite::HoverState hover;

	editor.View.Syntax = &syntax;
	editor.View.Diagnostics = &diagnostics;
	editor.View.Hover = &hover;

	CodeFaceMeasure face;
	editor.Frames.Next([&] {
		editor.Draw();
		face = MeasureCodeFace("v");
	});

	const ImVec2 lineStart = editor.CaretScreenPos();
	const float tokenLeft = lineStart.x + face.CellWidth * static_cast<float>(kTokenColumn);

	editor.HoverAt(ImVec2(tokenLeft + face.CellWidth * kHalf, lineStart.y + kEditorLineHeight * kHalf));
	editor.Next();

	const ImGuiWindow* tooltip = ShownHoverTooltip();
	REQUIRE(tooltip != nullptr);

	CHECK(tooltip->Pos.y == lineStart.y + kEditorLineHeight);
	CHECK(std::abs(tooltip->Pos.x - tokenLeft) < kJustInside);
	CHECK(editor.StackAfter == editor.StackBefore);
}
