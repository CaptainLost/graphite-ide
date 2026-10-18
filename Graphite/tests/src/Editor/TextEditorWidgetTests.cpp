#include <doctest/doctest.h>

#include "ImGuiFrames.h"

#include "Editor/TextEditorWidget.h"
#include "Editor/Find/FindBar.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Overlays/LensModel.h"
#include "Git/Changes/ChangeMarkers.h"

#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuItem.h"
#include "Core/ImGui/FontSet.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

	constexpr float kEditorFontSize = 20.0f;
	constexpr float kDocumentWidth = 800.0f;
	constexpr float kDocumentHeight = 400.0f;
	constexpr float kIntoTheRow = 2.0f;
	constexpr float kJustInside = 1.0f;
	constexpr float kJustPast = 1.0f;
	constexpr float kScanStep = 1.0f;
	constexpr float kHalf = 0.5f;
	constexpr std::size_t kFirstRow = 0;
	constexpr std::size_t kReferenceCount = 3;
	constexpr const char* kLensLabel = "3 references";
	constexpr const char* kTooltipWindow = "##Tooltip_00";

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
}

TEST_CASE("A lens label is hit across the width it is drawn at")
{
	EditorFrames editor("void Draw();\n");

	Graphite::LensModel lenses;
	lenses.SetDeclarations({Graphite::Declaration{.Name = "Draw",
												  .Kind = Graphite::DeclarationKind::Function,
												  .Line = 0,
												  .Column = Graphite::Utf16Column(0)}},
						   editor.Item.Rows, BufferOf(editor.Item), editor.Item.TabWidth);
	lenses.SetCount(kFirstRow, kReferenceCount);
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
	const float rowMiddleY = caret.y + sizes.CodeLineHeight * kHalf;

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
