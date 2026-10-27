#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "HoveredItems.h"
#include "DrawnGlyphs.h"
#include "ImGuiFrames.h"

#include "Editor/TextEditorWidget.h"
#include "Editor/Authors/AuthorCard.h"
#include "Editor/Authors/LinePeek.h"
#include "Editor/Authors/LinePeekContent.h"
#include "Editor/EditorCommandTarget.h"
#include "Editor/EditorCommands.h"
#include "Editor/EditorWidgetState.h"
#include "Editor/ScrollAnchor.h"
#include "Editor/Find/FindBar.h"
#include "Editor/Hover/HoverState.h"
#include "Editor/Overlays/DiagnosticOverlay.h"
#include "Editor/Overlays/LensModel.h"
#include "Git/Blame/HeadBlame.h"
#include "Git/Blame/LineAuthors.h"
#include "Git/Changes/ChangeMarkers.h"
#include "Language/Registry/LanguageRegistry.h"
#include "Language/Syntax/SyntaxCache.h"
#include "Theme/BuiltInSchemes.h"
#include "Theme/ColorScheme.h"
#include "Theme/EditorPalette.h"
#include "Theme/Palettes.h"

#include "Core/Base/Color.h"
#include "Core/Command/CommandRegistry.h"
#include "Core/Command/Keymap.h"
#include "Core/Command/MenuItem.h"
#include "Core/Git/Blame.h"
#include "Core/Git/Commit.h"
#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/Scrollbar.h"
#include "Core/ImGui/TextMetrics.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/Editing.h"
#include "Core/Text/TextDocument.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

	constexpr float kEditorFontSize = 20.0f;
	constexpr float kEditorLineHeight = 22.0f;
	constexpr float kTextOffset = 1.0f;
	constexpr float kLensFontFactor = 0.78f;
	constexpr float kLensFontSize = 16.0f;
	constexpr float kLensTextOffset = 3.0f;
	constexpr float kSquiggleAmplitude = 1.5f;
	constexpr float kHairlineWidth = 1.0f;
	constexpr float kDocumentWidth = 800.0f;
	constexpr float kDocumentHeight = 400.0f;
	constexpr float kIntoTheRow = 2.0f;
	constexpr float kJustInside = 1.0f;
	constexpr float kJustPast = 1.0f;
	constexpr float kScanStep = 1.0f;
	constexpr float kHalf = 0.5f;
	constexpr float kChangeStripWidth = 4.0f;
	constexpr std::size_t kFirstRow = 0;
	constexpr std::size_t kFirstLine = 0;
	constexpr std::size_t kSecondLine = 1;
	constexpr std::size_t kReferenceCount = 3;
	constexpr const char* kLensLabel = "3 references";
	constexpr const char* kTooltipWindow = "##Tooltip_00";
	constexpr std::string_view kEditorSurface = "##EditorSurface";
	constexpr std::string_view kHoverTooltipPrefix = "##HoverTooltip";
	constexpr std::string_view kAuthorCardPrefix = "##AuthorCard";

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
		float Width = kDocumentWidth;
		float Height = kDocumentHeight;

		explicit EditorFrames(std::string content)
			: Item(ItemWith(std::move(content)))
		{
			View.FontSize = kEditorFontSize;
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(Width, Height));
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

	struct Region
	{
		float Left = -FLT_MAX;
		float Right = FLT_MAX;
		float Top = -FLT_MAX;
		float Bottom = FLT_MAX;

		bool Holds(ImVec2 point) const
		{
			return point.x >= Left && point.x <= Right && point.y >= Top && point.y <= Bottom;
		}
	};

	std::optional<VerticalExtent> VerticalExtentInColourWithin(const ImDrawList& drawing, ImU32 colour,
																const Region& region)
	{
		VerticalExtent extent;
		bool foundVertex = false;

		for (const ImDrawVert& vertex : drawing.VtxBuffer)
		{
			if (vertex.col != colour || !region.Holds(vertex.pos))
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

	struct DrawnRegion
	{
		float Left = FLT_MAX;
		float Top = FLT_MAX;
		float Bottom = -FLT_MAX;
		std::size_t LastIndex = 0;
	};

	std::optional<DrawnRegion> DrawnIn(const ImDrawList& drawing, ImU32 colour, const Region& region)
	{
		DrawnRegion drawn;
		bool found = false;

		for (int index = 0; index < drawing.VtxBuffer.Size; ++index)
		{
			const ImDrawVert& vertex = drawing.VtxBuffer[index];

			if (vertex.col != colour || !region.Holds(vertex.pos))
			{
				continue;
			}

			found = true;
			drawn.Left = std::min(drawn.Left, vertex.pos.x);
			drawn.Top = std::min(drawn.Top, vertex.pos.y);
			drawn.Bottom = std::max(drawn.Bottom, vertex.pos.y);
			drawn.LastIndex = static_cast<std::size_t>(index);
		}

		if (!found)
		{
			return std::nullopt;
		}

		return drawn;
	}

	struct VisibleLeft
	{
		float Left = FLT_MAX;
		ImVec4 Clip;
	};

	// The leftmost point of the triangles drawn in `colour` that their command's clip rectangle lets through, with
	// that rectangle: a vertex past the clip is in the draw list but never on screen.
	std::optional<VisibleLeft> VisibleLeftInColour(const ImDrawList& drawing, ImU32 colour)
	{
		constexpr unsigned int kCornersInATriangle = 3;

		std::optional<VisibleLeft> found;

		for (const ImDrawCmd& command : drawing.CmdBuffer)
		{
			for (unsigned int corner = 0; corner + kCornersInATriangle <= command.ElemCount; corner += kCornersInATriangle)
			{
				float left = FLT_MAX;
				bool inColour = true;

				for (unsigned int each = 0; each < kCornersInATriangle; ++each)
				{
					const ImDrawVert& vertex =
						drawing.VtxBuffer[command.VtxOffset + drawing.IdxBuffer[command.IdxOffset + corner + each]];
					inColour = inColour && vertex.col == colour;
					left = std::min(left, vertex.pos.x);
				}

				const float shown = std::max(left, command.ClipRect.x);

				if (inColour && (!found.has_value() || shown < found->Left))
				{
					found = VisibleLeft{.Left = shown, .Clip = command.ClipRect};
				}
			}
		}

		return found;
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

	// WARNING: An anti-aliased stroke's quad reaches a pixel past its ink on either side, and further at a joint,
	// so the vertices read off the draw list would stand above and below the line that is seen. Without
	// anti-aliasing the quad is the stroke.
	void DrawLinesWithoutAntiAliasing()
	{
		ImGui::GetStyle().AntiAliasedLines = false;
	}

	Graphite::ResolvedScheme DarkSchemeWithTextIn(Core::Color text)
	{
		Graphite::ResolvedScheme scheme = Graphite::DarkScheme();
		scheme.Editor[static_cast<std::size_t>(Graphite::EditorColor::Text)] = text;

		return scheme;
	}

	float TextTopOfRow(float rowTop)
	{
		return rowTop + kTextOffset;
	}

	float TextBottomOfRow(float rowTop)
	{
		return TextTopOfRow(rowTop) + kEditorFontSize;
	}

	constexpr std::string_view kTwoLines = "first\nsecond\n";
	constexpr std::size_t kTwoLinesLineCount = 3;
	constexpr std::size_t kBothTextLines = 2;
	constexpr std::size_t kOnlyTheFirstLine = 1;
	constexpr std::uint8_t kAnnsCommitByte = 0xA1;
	constexpr std::chrono::sys_seconds kCommittedAt{std::chrono::seconds{1'700'000'000}};
	constexpr std::chrono::days kThreeMonths{90};

	// A table for kTwoLines whose first `committed` lines Ann last changed in a commit titled `title`; the rest are
	// Unknown.
	Graphite::LineAuthors AuthorsByAnn(std::size_t lineCount, std::size_t committed, std::string title)
	{
		Core::Oid commit;
		commit.Bytes.fill(kAnnsCommitByte);

		auto blame = std::make_shared<Core::Blame>();
		blame->File = "widget.txt";
		blame->LineCount = committed;
		blame->Hunks = {Core::BlameHunk{.FirstLine = 1,
										.LineCount = committed,
										.Commit = commit,
										.OriginalPath = "widget.txt",
										.OriginalFirstLine = 1}};

		Core::CommitSummary& summary = blame->Commits[commit];
		summary.Id = commit;
		summary.Author = Core::Signature{.Name = "Ann", .Email = "ann@example.com", .When = kCommittedAt};
		summary.Summary = std::move(title);

		auto head = std::make_shared<Graphite::HeadBlame>();
		head->Tracked = true;
		head->Blame = std::move(blame);
		head->HeadText = std::string{kTwoLines};

		Graphite::LineAuthors authors;
		authors.Head = std::move(head);
		authors.Lines.resize(lineCount);

		for (std::size_t line = 0; line < committed && line < lineCount; ++line)
		{
			authors.Lines[line] = Graphite::LineAuthor{.Kind = Graphite::LineAuthorKind::Committed,
													   .Hunk = 0,
													   .HeadLine = line + 1};
		}

		return authors;
	}

	void ShowAuthors(EditorFrames& editor, const Graphite::LineAuthors& authors)
	{
		editor.View.Author.Authors = &authors;
		editor.View.Author.Now = kCommittedAt + kThreeMonths;
	}

	std::vector<DrawnGlyph> SurfaceGlyphsIn(ImU32 colour)
	{
		const ImGuiWindow* surface = EditorSurface();
		REQUIRE(surface != nullptr);

		std::vector<DrawnGlyph> found;

		for (const DrawnGlyph& glyph : DrawnGlyphs(*surface->DrawList))
		{
			if (glyph.Colour == colour)
			{
				found.push_back(glyph);
			}
		}

		return found;
	}

	bool WithinRow(const DrawnGlyph& glyph, float rowTop)
	{
		return glyph.Box.Min.y >= rowTop && glyph.Box.Max.y <= rowTop + kEditorLineHeight;
	}

	ImGuiWindow* ShownAuthorCard()
	{
		for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			const bool child = (window->Flags & ImGuiWindowFlags_ChildWindow) != 0;

			if (!child && std::string_view{window->Name}.starts_with(kAuthorCardPrefix) && window->Active)
			{
				return window;
			}
		}

		return nullptr;
	}

	// ImGui keeps no item's position past its frame, so the pointer walks the card from its foot up, a row at a
	// time, until ImGui reports the item with this label under it.
	std::optional<ImVec2> FindInAuthorCard(EditorFrames& editor, const char* label)
	{
		constexpr float kStep = 4.0f;

		ImGuiWindow* const card = ShownAuthorCard();

		if (card == nullptr)
		{
			return std::nullopt;
		}

		const ImGuiID item = card->GetID(label);
		const ImRect box = card->Rect();

		for (float y = box.Max.y - kStep; y > box.Min.y; y -= kStep)
		{
			for (float x = box.Min.x + kStep; x < box.Max.x; x += kStep)
			{
				editor.HoverAt(ImVec2(x, y));

				if (ImGui::GetCurrentContext()->HoveredId == item)
				{
					return ImVec2(x, y);
				}
			}
		}

		return std::nullopt;
	}

	// ImGui names a child window "<parent>/<label>_<id>", so a label's slash and underscore pick out that child alone.
	constexpr std::string_view kPeekWindowMark = "/##LinePeek_";
	constexpr std::string_view kPeekListMark = "/##LinePeekList_";
	constexpr std::string_view kPeekDetailMark = "/##LinePeekDiff_";
	constexpr bool kJustOpened = true;

	ImGuiWindow* ActiveWindowNamedWith(std::string_view mark)
	{
		for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (window->Active && std::string_view{window->Name}.find(mark) != std::string_view::npos)
			{
				return window;
			}
		}

		return nullptr;
	}

	// The surface itself, not one of the children named under it.
	ImGuiWindow* MutableEditorSurface()
	{
		for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			const std::string_view name{window->Name};
			const std::size_t mark = name.find(kEditorSurface);

			if (mark != std::string_view::npos && name.find('/', mark) == std::string_view::npos)
			{
				return window;
			}
		}

		return nullptr;
	}

	std::string LinesOf(std::size_t count)
	{
		std::string content;

		for (std::size_t line = 0; line < count; ++line)
		{
			content += "line\n";
		}

		return content;
	}

	Graphite::LinePeekContent PeekOfTwoCommits()
	{
		Graphite::LinePeekContent content;
		content.State = Graphite::PeekState::Ready;
		content.Header = "History of line 1";
		content.Entries = {Graphite::PeekEntry{.Title = "Fix",
											   .MetaName = "Ann",
											   .Byline = "Ann",
											   .Lines = {{Core::DiffLineKind::Removed, "frist", true},
														 {Core::DiffLineKind::Added, "first", true}},
											   .Commit = Core::Oid{}},
						   Graphite::PeekEntry{.Title = "Add",
											   .MetaName = "Ann",
											   .Byline = "Ann",
											   .Lines = {{Core::DiffLineKind::Added, "frist", true}},
											   .Commit = Core::Oid{}}};

		return content;
	}

	// An entry whose diff is `lineCount` added lines reading `text`, the one at `tracked` the line the peek follows.
	Graphite::PeekEntry LongEntry(const char* title, std::string_view text, std::size_t lineCount, std::size_t tracked)
	{
		Graphite::PeekEntry entry{.Title = title, .MetaName = "Ann", .Byline = "Ann", .Commit = Core::Oid{}};

		for (std::size_t line = 0; line < lineCount; ++line)
		{
			entry.Lines.push_back({Core::DiffLineKind::Added, std::string{text}, line == tracked});
		}

		return entry;
	}

	Graphite::LinePeekContent ReadyPeek(std::string header, std::vector<Graphite::PeekEntry> entries)
	{
		Graphite::LinePeekContent content;
		content.State = Graphite::PeekState::Ready;
		content.Header = std::move(header);
		content.Entries = std::move(entries);

		return content;
	}

	// The accent the peek draws its bar in, beside the tracked line in the detail.
	ImU32 PeekAccent()
	{
		return ImGui::ColorConvertFloat4ToU32(
			Core::ToImVec4(Graphite::ColourFromScheme(Graphite::DarkScheme(), Graphite::ChromeColor::Accent)));
	}

	void ShowPeek(EditorFrames& editor, std::size_t line, const Graphite::LinePeekContent& content,
				  bool justOpened = false)
	{
		editor.View.Peek = Graphite::LinePeekView{.Line = line, .Content = &content, .JustOpened = justOpened};
	}

	// The pointer walks down the window near its left edge, a step at a time, until ImGui reports the item with this
	// label under it.
	std::optional<ImVec2> FindDownTheLeftOf(EditorFrames& editor, std::string_view windowMark, const char* label)
	{
		constexpr float kStep = 2.0f;
		constexpr float kIntoTheWindow = 8.0f;

		ImGuiWindow* const window = ActiveWindowNamedWith(windowMark);

		if (window == nullptr)
		{
			return std::nullopt;
		}

		const ImGuiID item = window->GetID(label);
		const ImRect box = window->Rect();
		const float x = box.Min.x + kIntoTheWindow;

		for (float y = box.Min.y + kStep; y < box.Max.y; y += kStep)
		{
			editor.HoverAt(ImVec2(x, y));

			if (ImGui::GetCurrentContext()->HoveredId == item)
			{
				return ImVec2(x, y);
			}
		}

		return std::nullopt;
	}

	float RowsTall(std::size_t rows)
	{
		return static_cast<float>(rows) * kEditorLineHeight;
	}

	// Stands in for DocumentWindows, the one real target, where Line History is concerned: it takes the line the way
	// that does, from the offset the editor recorded when its context menu opened.
	class LineHistoryTarget final : public Graphite::EditorCommandTarget
	{
	public:
		LineHistoryTarget(Core::OpenItem& item, const Graphite::EditorWidgetState& widget)
			: m_Item(&item), m_Widget(&widget)
		{
		}

		std::optional<std::size_t> OpenedOn;

		bool CanRun(Graphite::EditorAction) override { return false; }
		void Queue(Graphite::EditorAction, bool) override {}
		bool CanSave() override { return false; }
		void Save() override {}
		bool CanClose() override { return false; }
		void Close() override {}
		bool CanFind() override { return false; }
		void OpenFind() override {}
		std::optional<std::filesystem::path> CaretFile() override { return std::nullopt; }
		void GoToDefinition() override {}
		void FindReferences(Graphite::ReferencesPanel&) override {}
		bool CanShowLineHistory() override { return true; }

		void ShowLineHistory(bool fromMenu) override
		{
			OpenedOn = Graphite::PlaceCaretForLineHistory(*m_Item, fromMenu ? m_Widget->ContextMenuOffset : std::nullopt);
		}

	private:
		Core::OpenItem* m_Item = nullptr;
		const Graphite::EditorWidgetState* m_Widget = nullptr;
	};
}

TEST_CASE("A lens label is drawn at 0.78 of the editor size and hit across that width")
{
	EditorFrames editor("void Draw();\n");

	Graphite::LensModel lenses;
	DeclareReferencedFirstLine(lenses, editor.Item);
	editor.View.Lens = &lenses;

	float labelWidth = 0.0f;
	float editorSizeWidth = 0.0f;

	editor.Frames.Next([&] {
		editor.Draw();
		labelWidth = InterfaceWidthAt(kEditorFontSize * kLensFontFactor, kLensLabel);
		editorSizeWidth = InterfaceWidthAt(kEditorFontSize, kLensLabel);
	});

	REQUIRE(labelWidth < editorSizeWidth - kJustInside);

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

TEST_CASE("Lens rows hidden while no server runs for the file take no room until they are shown")
{
	EditorFrames editor("void Draw();\n");
	editor.Next();

	const ImVec2 withoutLenses = editor.CaretScreenPos();

	Graphite::LensModel lenses;
	DeclareReferencedFirstLine(lenses, editor.Item);
	editor.View.Lens = &lenses;
	editor.View.ShowLensRows = false;
	editor.Next();

	CHECK(editor.CaretScreenPos().y == doctest::Approx(withoutLenses.y));

	editor.View.ShowLensRows = true;
	editor.Next();

	CHECK(editor.CaretScreenPos().y == doctest::Approx(withoutLenses.y + kEditorLineHeight));
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("A lens with no count yet says what it waits for in the lens colour and takes no click")
{
	EditorFrames editor("void Draw();\n");
	editor.Next();

	const ImVec2 lensRowStart = editor.CaretScreenPos();

	Graphite::LensModel lenses;
	lenses.SetDeclarations({Graphite::Declaration{.Name = "Draw",
												  .Kind = Graphite::DeclarationKind::Function,
												  .Line = kFirstLine,
												  .Column = Graphite::Utf16Column(0)}},
						   editor.Item.Rows, BufferOf(editor.Item), editor.Item.TabWidth);
	lenses.SetWaitingText("waiting for clangd to index");
	editor.View.Lens = &lenses;
	editor.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const std::optional<VerticalExtent> words = VerticalExtentInColourWithin(
		*surface->DrawList, DarkEditorPalette().Lens,
		Region{.Left = lensRowStart.x, .Top = lensRowStart.y, .Bottom = lensRowStart.y + kEditorLineHeight});
	CHECK(words.has_value());

	editor.LensClicked.reset();
	editor.Click(ImVec2(lensRowStart.x + kIntoTheRow, lensRowStart.y + kEditorLineHeight * kHalf));
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

TEST_CASE("An editor line is nineteen pixels at seventeen")
{
	constexpr float kSeventeen = 17.0f;
	constexpr float kNineteen = 19.0f;

	EditorFrames editor("first\nsecond\n");
	editor.View.FontSize = kSeventeen;

	editor.Next();
	const float firstLineTop = editor.CaretScreenPos().y;

	PlaceCaret(editor.Item, BufferOf(editor.Item).LineStart(kSecondLine));
	editor.Next();

	CHECK(editor.CaretScreenPos().y - firstLineTop == kNineteen);
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

TEST_CASE("A double click on the right half of a word's last character selects the word")
{
	constexpr std::size_t kLastCharacter = 5;
	constexpr std::size_t kWordEnd = 6;
	constexpr float kIntoTheRightHalf = 0.75f;

	EditorFrames editor("_steam;\n");
	editor.Next();

	PlaceCaret(editor.Item, kLastCharacter);
	editor.Next();
	const ImVec2 lastCharacter = editor.CaretScreenPos();

	PlaceCaret(editor.Item, kWordEnd);
	editor.Next();
	const float wordEndX = editor.CaretScreenPos().x;

	const ImVec2 rightHalf(lastCharacter.x + (wordEndX - lastCharacter.x) * kIntoTheRightHalf,
						   lastCharacter.y + kEditorLineHeight * kHalf);
	editor.Click(rightHalf);
	editor.Click(rightHalf);

	CHECK(Core::SelectionRange(editor.Item.Caret) == Core::OffsetRange{.From = 0, .To = kWordEnd});
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
	const std::optional<VerticalExtent> caret =
		VerticalExtentInColourWithin(*surface->DrawList, DarkEditorPalette().Caret,
									 Region{.Left = caretX - halfCell, .Right = caretX + halfCell});
	const std::optional<VerticalExtent> glyph = VerticalExtentInColourWithin(
		*surface->DrawList, DarkEditorPalette().Text,
		Region{.Left = lineStart.x - halfCell, .Right = lineStart.x + face.CellWidth + halfCell});
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
		Core::PushInterfaceFont(kLensFontSize);
		ink = InkOf(kLensLabel);
		ImGui::PopFont();
	});

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const std::optional<VerticalExtent> label = VerticalExtentInColour(*surface->DrawList, DarkEditorPalette().Lens);
	REQUIRE(label.has_value());

	CHECK(label->Top - ink.Top == doctest::Approx(lensRowTop + kLensTextOffset));
	CHECK(label->Bottom - ink.Bottom == doctest::Approx(lensRowTop + kLensTextOffset));
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

TEST_CASE("A diagnostic squiggle stays within its text's box by no more than its amplitude")
{
	constexpr std::size_t kSpanColumn = 4;
	constexpr std::size_t kSpanEndColumn = 9;

	EditorFrames editor("int value = 0;\n");
	DrawLinesWithoutAntiAliasing();

	Graphite::DiagnosticOverlay diagnostics;
	diagnostics.Set({ErrorAt(kFirstLine, kSpanColumn, kSpanEndColumn, "unused variable")}, BufferOf(editor.Item),
					editor.Item.TabWidth);
	editor.View.Diagnostics = &diagnostics;

	editor.Next();

	const float lineTop = editor.CaretScreenPos().y;

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const std::optional<VerticalExtent> squiggle =
		VerticalExtentInColour(*surface->DrawList, DarkEditorPalette().DiagnosticError);
	REQUIRE(squiggle.has_value());

	const float amplitude = Core::Scaled(kSquiggleAmplitude);

	CHECK(squiggle->Bottom <= TextBottomOfRow(lineTop) + amplitude);
	CHECK(squiggle->Top >= TextTopOfRow(lineTop) + kEditorFontSize * kHalf);
}

TEST_CASE("The Ctrl-hover underline sits at the text's foot rather than the line's")
{
	constexpr std::size_t kTokenColumn = 4;
	constexpr std::size_t kTokenEndColumn = 9;
	constexpr Core::Color kTextSeenOnlyInTheUnderline{.R = 0.0f, .G = 1.0f, .B = 0.0f};

	EditorFrames editor("int value = 0;\n");
	DrawLinesWithoutAntiAliasing();

	const Graphite::ResolvedScheme scheme = DarkSchemeWithTextIn(kTextSeenOnlyInTheUnderline);

	Graphite::SyntaxCache syntax;
	syntax.SetSyntax(Graphite::SyntaxOf(Graphite::LanguageId::Cpp));

	editor.View.Scheme = &scheme;
	editor.View.Syntax = &syntax;

	CodeFaceMeasure face;
	editor.Frames.Next([&] {
		editor.Draw();
		face = MeasureCodeFace("v");
	});

	const ImVec2 lineStart = editor.CaretScreenPos();
	const float tokenLeft = lineStart.x + face.CellWidth * static_cast<float>(kTokenColumn);
	const float tokenRight = lineStart.x + face.CellWidth * static_cast<float>(kTokenEndColumn);

	ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
	editor.HoverAt(ImVec2(tokenLeft + face.CellWidth * kHalf, lineStart.y + kEditorLineHeight * kHalf));
	editor.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const std::optional<VerticalExtent> underline =
		VerticalExtentInColourWithin(*surface->DrawList, Graphite::ReadEditorPalette(scheme).Text,
									 Region{.Left = tokenLeft, .Right = tokenRight + face.CellWidth * kHalf});
	REQUIRE(underline.has_value());

	CHECK(underline->Bottom == doctest::Approx(TextBottomOfRow(lineStart.y)));
	CHECK(underline->Top == doctest::Approx(TextBottomOfRow(lineStart.y) - Core::Scaled(kHairlineWidth)));
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("A hovered lens label is underlined at its text's foot")
{
	EditorFrames editor("void Draw();\n");
	DrawLinesWithoutAntiAliasing();
	editor.Next();

	const ImVec2 lensRowStart = editor.CaretScreenPos();

	Graphite::LensModel lenses;
	DeclareReferencedFirstLine(lenses, editor.Item);
	editor.View.Lens = &lenses;
	editor.Next();

	editor.HoverAt(ImVec2(lensRowStart.x + kIntoTheRow, lensRowStart.y + kEditorLineHeight * kHalf));
	editor.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const std::optional<VerticalExtent> label = VerticalExtentInColourWithin(
		*surface->DrawList, DarkEditorPalette().Text,
		Region{.Left = lensRowStart.x, .Top = lensRowStart.y, .Bottom = lensRowStart.y + kEditorLineHeight});
	REQUIRE(label.has_value());

	CHECK(label->Bottom == doctest::Approx(lensRowStart.y + kLensTextOffset + kLensFontSize));
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("The current-line fill starts at the editor's left edge")
{
	EditorFrames editor("first line\nsecond line\n");
	editor.Next();
	editor.Next();

	const float lineTop = editor.CaretScreenPos().y;
	const Graphite::EditorPalette palette = DarkEditorPalette();

	const ImGuiWindow* surface = EditorSurface();
	const ImGuiWindow* document = ImGui::FindWindowByName("Document");
	REQUIRE(surface != nullptr);
	REQUIRE(document != nullptr);
	REQUIRE(surface->Pos.x > document->Pos.x);

	const Region firstRow{.Top = lineTop, .Bottom = lineTop + kEditorLineHeight - kJustInside};
	const std::optional<DrawnRegion> fill = DrawnIn(*surface->DrawList, palette.GutterFill, firstRow);
	const std::optional<DrawnRegion> highlight = DrawnIn(*surface->DrawList, palette.CurrentLine, Region{});
	const std::optional<VisibleLeft> shown = VisibleLeftInColour(*surface->DrawList, palette.CurrentLine);
	REQUIRE(fill.has_value());
	REQUIRE(highlight.has_value());
	REQUIRE(shown.has_value());

	CHECK(fill->Left == surface->Pos.x);
	CHECK(highlight->Left == document->Pos.x);
	CHECK(shown->Left == document->Pos.x);
	CHECK(shown->Clip.y == surface->InnerClipRect.Min.y);
	CHECK(shown->Clip.w == surface->InnerClipRect.Max.y);
	CHECK(highlight->Top == lineTop);
	CHECK(highlight->Bottom == lineTop + kEditorLineHeight);
	CHECK(highlight->LastIndex > fill->LastIndex);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("An edit through the editor moves the declarations it is given with their lines")
{
	EditorFrames editor("a\nb\nc\n");
	editor.Next();

	PlaceCaret(editor.Item, 0);
	REQUIRE(Core::InsertText(editor.Item, "x\n"));

	Graphite::Declaration declaration;
	declaration.Name = "Draw";
	declaration.Kind = Graphite::DeclarationKind::Function;
	declaration.Line = 3;
	declaration.NameLine = 3;
	declaration.EndLine = 3;

	std::vector<Graphite::Declaration> declarations{declaration};
	Graphite::EditorWidgetState widget;
	widget.PendingActions.push_back(Graphite::PendingEditorAction{.Action = Graphite::EditorAction::Undo});

	editor.View.Declarations = &declarations;
	editor.View.Widget = &widget;
	editor.Next();

	REQUIRE(BufferOf(editor.Item).Materialize() == "a\nb\nc\n");
	REQUIRE(declarations.size() == 1);
	CHECK(declarations[0].Line == 2);
	CHECK(declarations[0].EndLine == 2);
}

TEST_CASE("Every other occurrence of a selected word is filled in the occurrence colour")
{
	constexpr int kVerticesPerFill = 4;
	constexpr int kOtherOccurrences = 2;

	EditorFrames editor("alpha beta alpha\nalpha\n");
	editor.View.HighlightOccurrences = true;
	editor.Item.Caret = Core::Caret{.Offset = 5, .Anchor = 0};
	editor.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	CHECK(VerticesInColour(*surface->DrawList, DarkEditorPalette().SelectionOccurrence) ==
		  kOtherOccurrences * kVerticesPerFill);
}

TEST_CASE("No occurrence is marked in the text or on the scrollbar while highlighting is off")
{
	EditorFrames editor("alpha beta alpha\nalpha\n");
	editor.View.HighlightOccurrences = false;
	editor.Item.Caret = Core::Caret{.Offset = 5, .Anchor = 0};
	editor.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const ImU32 fill = DarkEditorPalette().SelectionOccurrence;

	CHECK_FALSE(DrawsInColour(*surface->DrawList, fill));
	CHECK_FALSE(DrawsInColour(*surface->DrawList, Core::WithOpaqueAlpha(fill)));
}

TEST_CASE("Occurrences are ticked on the scrollbar either side of its grab, never over it")
{
	constexpr std::size_t kLineCount = 200;
	constexpr std::size_t kSecondOccurrenceLine = 150;

	std::string content;

	for (std::size_t line = 0; line < kLineCount; ++line)
	{
		content += line == 0 || line == kSecondOccurrenceLine ? "alpha\n" : "x\n";
	}

	EditorFrames editor(std::move(content));
	editor.View.HighlightOccurrences = true;
	editor.Item.Caret = Core::Caret{.Offset = 5, .Anchor = 0};
	editor.Next();
	editor.Next();

	ImGuiWindow* surface = const_cast<ImGuiWindow*>(EditorSurface());
	REQUIRE(surface != nullptr);
	REQUIRE(surface->ScrollbarY);

	const ImRect bar = ImGui::GetWindowScrollbarRect(surface, ImGuiAxis_Y);
	const float inset = Core::ScrollbarGrabInset(ImGui::GetStyle().ScrollbarPadding, bar.GetSize());
	const float grabLeft = bar.Min.x + inset;
	const float grabRight = bar.Max.x - inset;
	const ImU32 tick = Core::WithOpaqueAlpha(DarkEditorPalette().SelectionOccurrence);

	REQUIRE(grabRight - grabLeft > kChangeStripWidth);

	int leftOfGrab = 0;
	int rightOfGrab = 0;
	int overGrab = 0;

	for (const ImDrawVert& vertex : surface->DrawList->VtxBuffer)
	{
		if (vertex.col != tick)
		{
			continue;
		}

		if (vertex.pos.x < grabLeft)
		{
			++leftOfGrab;
		}
		else if (vertex.pos.x > grabRight)
		{
			++rightOfGrab;
		}
		else
		{
			++overGrab;
		}
	}

	CHECK(leftOfGrab > 0);
	CHECK(rightOfGrab > 0);
	CHECK(overGrab == 0);

	const std::optional<VerticalExtent> ticks = VerticalExtentInColour(*surface->DrawList, tick);
	REQUIRE(ticks.has_value());

	CHECK(ticks->Top >= bar.Min.y);
	CHECK(ticks->Bottom <= bar.Max.y);
	CHECK(ticks->Bottom - ticks->Top > bar.GetHeight() * kHalf);
}

namespace {

	bool AnyWindowNamedLike(std::string_view part)
	{
		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(part) != std::string_view::npos)
			{
				return true;
			}
		}

		return false;
	}
}

TEST_CASE("A tab whose file is still being read draws no editor and asks for nothing")
{
	EditorFrames frames{""};
	frames.Item = Core::OpenItem{.Path = "C:/p/big.log", .FileSize = 100ull * 1024 * 1024};
	frames.Item.LoadTicket = 1;

	frames.Next();
	frames.Next();

	CHECK(frames.StackAfter == frames.StackBefore);
	CHECK_FALSE(AnyWindowNamedLike(kEditorSurface));
	CHECK_FALSE(frames.Result.RetryPressed);
	CHECK_FALSE(frames.Result.DrewRows);
}

TEST_CASE("A tab whose file could not be read offers Retry and reports the press")
{
	EditorFrames frames{""};
	frames.Item = Core::OpenItem{.Path = "C:/p/locked.log", .Failure = Core::OpenFailure::ReadFailed,
								 .FailureDetail = "Cannot open file: C:/p/locked.log"};

	const float middleX = kDocumentWidth * kHalf;
	const std::vector<HoveredStretch> stretches = HoveredItems::Walk(
		frames.Frames, HoveredItems::Along::Down, middleX, 0.0f, kDocumentHeight, [&frames] { frames.Draw(); });

	REQUIRE(stretches.size() == 1);

	frames.Frames.Click(ImVec2(middleX, stretches.front().Middle()), [&frames] { frames.Draw(); });

	CHECK(frames.Result.RetryPressed);
	CHECK_FALSE(AnyWindowNamedLike(kEditorSurface));
}

TEST_CASE("A tab refused as binary says why and offers nothing to press")
{
	EditorFrames frames{""};
	frames.Item = Core::OpenItem{.Path = "C:/p/tool.exe", .Failure = Core::OpenFailure::Binary, .FileSize = 4096};

	const std::vector<HoveredStretch> stretches = HoveredItems::Walk(
		frames.Frames, HoveredItems::Along::Down, kDocumentWidth * kHalf, 0.0f, kDocumentHeight, [&frames] { frames.Draw(); });

	CHECK(stretches.empty());
	CHECK_FALSE(frames.Result.RetryPressed);
}

TEST_CASE("A pane measures the same wrap before a document's text is there as once it is")
{
	EditorFrames frames{"short text\n"};
	frames.Next();
	REQUIRE(frames.Result.Wrap.has_value());
	const Graphite::WrapGeometry withText = *frames.Result.Wrap;

	frames.Item = Core::OpenItem{.Path = "C:/p/big.log", .FileSize = 100ull * 1024 * 1024};
	frames.Item.LoadTicket = 1;
	frames.Next();
	REQUIRE(frames.Result.Wrap.has_value());

	CHECK(*frames.Result.Wrap == withText);
}

TEST_CASE("A wrapped row loses a column to each line-number digit and gains the scrollbar's room when it is hidden")
{
	const Graphite::WrapGeometry geometry{.WidthBesideScrollbar = 500.0f,
										  .ScrollbarWidth = 20.0f,
										  .FixedGutterWidth = 30.0f,
										  .CellWidth = 10.0f,
										  .LineNumberGap = 5.0f};

	const std::size_t threeDigits = Graphite::WrappedColumns(geometry, 999, Graphite::VerticalScrollbar::Shown);
	const std::size_t fourDigits = Graphite::WrappedColumns(geometry, 1000, Graphite::VerticalScrollbar::Shown);
	const std::size_t scrollbarHidden = Graphite::WrappedColumns(geometry, 999, Graphite::VerticalScrollbar::Hidden);

	CHECK(threeDigits == 43);
	CHECK(fourDigits == threeDigits - 1);
	CHECK(scrollbarHidden == threeDigits + 2);
}
TEST_CASE("A tall wrapped document shows no horizontal scrollbar in the frame its vertical one arrives")
{
	std::string tallText;

	for (int line = 0; line < 2000; ++line)
	{
		tallText += "words that fill a row of the editor\n";
	}

	EditorFrames frames{tallText};
	frames.View.WordWrap = true;

	frames.Next();
	frames.Next();

	const ImGuiWindow* surface = nullptr;

	for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
	{
		if (std::string_view{window->Name}.find(kEditorSurface) != std::string_view::npos)
		{
			surface = window;
		}
	}

	REQUIRE(surface != nullptr);
	CHECK(surface->ScrollbarY);
	CHECK_FALSE(surface->ScrollbarX);
}
TEST_CASE("Where a document just fits its surface, the rows reserve room for a scrollbar only when one shows")
{
	constexpr int kFittingLines = 15;
	const float surfaceHeight = static_cast<float>(kFittingLines) * kEditorLineHeight;

	for (const int linesPastFit : {-1, 0, 1})
	{
		const int lineCount = kFittingLines + linesPastFit;
		std::string text;

		for (int line = 0; line < lineCount; ++line)
		{
			text += line + 1 < lineCount ? "a\n" : "a";
		}

		ImGuiFrames frames;
		Core::OpenItem item = ItemWith(text);
		Graphite::EditorView view;
		view.FontSize = kEditorFontSize;
		view.WordWrap = true;
		const Graphite::CommandContext context;
		Graphite::FindBar bar;
		Graphite::EditorFrameResult result;

		const auto draw = [&] {
			const float windowHeight = surfaceHeight + 2.0f * ImGui::GetStyle().WindowPadding.y;
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(kDocumentWidth, windowHeight));
			ImGui::Begin("Document", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			result = Graphite::DrawEditor(context, item, view, bar);
			ImGui::End();
		};

		frames.Next(draw);
		frames.Next(draw);
		frames.Next(draw);

		const ImGuiWindow* surface = nullptr;

		for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
		{
			if (std::string_view{window->Name}.find(kEditorSurface) != std::string_view::npos)
			{
				surface = window;
			}
		}

		REQUIRE(surface != nullptr);
		REQUIRE(result.Wrap.has_value());

		const bool scrollbarShown = surface->ScrollbarY;
		const Graphite::VerticalScrollbar shownOrHidden = scrollbarShown ? Graphite::VerticalScrollbar::Shown : Graphite::VerticalScrollbar::Hidden;

		CHECK(scrollbarShown == (linesPastFit > 0));
		CHECK(item.ColumnsPerWrappedRow == Graphite::WrappedColumns(*result.Wrap, static_cast<std::size_t>(lineCount), shownOrHidden));
	}
}

TEST_CASE("Line author is drawn after the caret line's text in the lens colour")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LineAuthors authors = AuthorsByAnn(kTwoLinesLineCount, kBothTextLines, "Fix");
	Graphite::EditorWidgetState widget;

	ShowAuthors(editor, authors);
	editor.View.Widget = &widget;
	editor.Next();
	editor.Next();

	const float lineTop = editor.CaretScreenPos().y;
	float textRight = -FLT_MAX;

	for (const DrawnGlyph& glyph : SurfaceGlyphsIn(DarkEditorPalette().Text))
	{
		if (WithinRow(glyph, lineTop))
		{
			textRight = std::max(textRight, glyph.Box.Max.x);
		}
	}

	REQUIRE(textRight > editor.CaretScreenPos().x);

	const std::vector<DrawnGlyph> author = SurfaceGlyphsIn(DarkEditorPalette().Lens);
	REQUIRE_FALSE(author.empty());
	REQUIRE(widget.AuthorTextRect.has_value());

	for (const DrawnGlyph& glyph : author)
	{
		CHECK(glyph.Box.Min.x > textRight);
		CHECK(WithinRow(glyph, lineTop));
		CHECK(widget.AuthorTextRect->Contains(glyph.Box.GetCenter()));
	}

	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Line author is not drawn on other lines")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LineAuthors authors = AuthorsByAnn(kTwoLinesLineCount, kBothTextLines, "Fix");

	ShowAuthors(editor, authors);
	PlaceCaret(editor.Item, BufferOf(editor.Item).LineStart(kSecondLine));
	editor.Next();
	editor.Next();

	const float secondLineTop = editor.CaretScreenPos().y;
	const std::vector<DrawnGlyph> author = SurfaceGlyphsIn(DarkEditorPalette().Lens);
	REQUIRE_FALSE(author.empty());

	for (const DrawnGlyph& glyph : author)
	{
		CHECK(WithinRow(glyph, secondLineTop));
	}
}

TEST_CASE("Line author is hidden while typing")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LineAuthors authors = AuthorsByAnn(kTwoLinesLineCount, kBothTextLines, "Fix");
	Graphite::EditorWidgetState widget;

	ShowAuthors(editor, authors);
	editor.View.Widget = &widget;
	editor.Next();
	editor.Next();
	REQUIRE(widget.AuthorTextRect.has_value());

	editor.View.Author.Hidden = true;
	editor.Next();

	CHECK(SurfaceGlyphsIn(DarkEditorPalette().Lens).empty());
	CHECK_FALSE(widget.AuthorTextRect.has_value());
}

TEST_CASE("Line author waiting for a rest is not drawn in the frame the caret reaches another line")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LineAuthors authors = AuthorsByAnn(kTwoLinesLineCount, kBothTextLines, "Fix");
	Graphite::EditorWidgetState widget;

	ShowAuthors(editor, authors);
	editor.View.Widget = &widget;
	editor.View.Author.WaitsForRest = true;
	editor.Next();
	editor.Next();
	REQUIRE(widget.AuthorTextRect.has_value());

	const ImVec2 firstLine = editor.CaretScreenPos();
	const ImVec2 secondLine(firstLine.x + kIntoTheRow, firstLine.y + kEditorLineHeight + kEditorLineHeight * kHalf);

	ImGui::GetIO().AddMousePosEvent(secondLine.x, secondLine.y);
	editor.Next();
	ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
	editor.Next();

	REQUIRE(CaretLine(editor.Item) == kSecondLine);
	CHECK(SurfaceGlyphsIn(DarkEditorPalette().Lens).empty());
	CHECK_FALSE(widget.AuthorTextRect.has_value());

	ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
	editor.Next();
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Line author draws nothing past a stale table")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LineAuthors authors = AuthorsByAnn(kOnlyTheFirstLine, kOnlyTheFirstLine, "Fix");

	ShowAuthors(editor, authors);
	PlaceCaret(editor.Item, BufferOf(editor.Item).LineStart(kSecondLine));
	editor.Next();
	editor.Next();

	CHECK(SurfaceGlyphsIn(DarkEditorPalette().Lens).empty());
}

TEST_CASE("Line author leaves the horizontal scroll range alone")
{
	constexpr int kTitleRepeats = 8;

	std::string longTitle;

	for (int repeat = 0; repeat < kTitleRepeats; ++repeat)
	{
		longTitle += "Iterate a copy so handlers can unsubscribe while publishing ";
	}

	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LineAuthors authors = AuthorsByAnn(kTwoLinesLineCount, kBothTextLines, longTitle);

	ShowAuthors(editor, authors);
	editor.Next();
	editor.Next();

	REQUIRE_FALSE(SurfaceGlyphsIn(DarkEditorPalette().Lens).empty());
	const float withAuthor = EditorSurface()->ScrollMax.x;

	editor.View.Author.Authors = nullptr;
	editor.Next();
	editor.Next();

	REQUIRE(SurfaceGlyphsIn(DarkEditorPalette().Lens).empty());
	CHECK(EditorSurface()->ScrollMax.x == withAuthor);
}

TEST_CASE("Author card opens over the author and Copy hash reports the line")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LineAuthors authors = AuthorsByAnn(kTwoLinesLineCount, kBothTextLines, "Fix");
	Graphite::EditorWidgetState widget;

	ShowAuthors(editor, authors);
	editor.View.Author.HoverDelaySeconds = 0.0f;
	editor.View.Widget = &widget;
	editor.Next();
	editor.Next();
	REQUIRE(widget.AuthorTextRect.has_value());

	editor.HoverAt(widget.AuthorTextRect->GetCenter());
	editor.Next();

	REQUIRE(ShownAuthorCard() != nullptr);
	CHECK(editor.StackAfter == editor.StackBefore);

	const std::optional<ImVec2> copyHash = FindInAuthorCard(editor, "Copy hash");
	REQUIRE(copyHash.has_value());

	const ImGuiWindow* const focusedBefore = ImGui::GetCurrentContext()->NavWindow;
	REQUIRE(focusedBefore != nullptr);

	editor.Click(*copyHash);

	CHECK(editor.Result.CardAction ==
		  Graphite::AuthorCardAction{.Action = Graphite::AuthorCardAction::Kind::CopyHash, .Line = kFirstLine});
	CHECK(ImGui::GetCurrentContext()->NavWindow == focusedBefore);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Author card opens on a click on the author without moving the caret")
{
	constexpr float kLongDelay = 10.0f;

	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LineAuthors authors = AuthorsByAnn(kTwoLinesLineCount, kBothTextLines, "Fix");
	Graphite::EditorWidgetState widget;

	ShowAuthors(editor, authors);
	editor.View.Author.HoverDelaySeconds = kLongDelay;
	editor.View.Widget = &widget;
	editor.Next();
	editor.Next();
	REQUIRE(widget.AuthorTextRect.has_value());

	const ImVec2 author = widget.AuthorTextRect->GetCenter();

	editor.HoverAt(author);

	CHECK(ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow);
	CHECK(ShownAuthorCard() == nullptr);

	editor.Click(author);

	CHECK(editor.Item.Caret.Offset == 0);
	CHECK(ShownAuthorCard() != nullptr);
}

TEST_CASE("Peek pushes the lines below it down by its rows")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LinePeekContent loading;

	editor.Next();
	const float firstLineTop = editor.CaretScreenPos().y;

	ShowPeek(editor, kFirstLine, loading);
	PlaceCaret(editor.Item, BufferOf(editor.Item).LineStart(kSecondLine));
	editor.Next();

	CHECK(editor.CaretScreenPos().y - firstLineTop == RowsTall(Graphite::kLinePeekRows + 1));
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek under the last line draws below it")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LinePeekContent loading;

	editor.Next();
	editor.Next();
	const float withoutPeek = EditorSurface()->ContentSize.y;

	ShowPeek(editor, kTwoLinesLineCount - 1, loading);
	editor.Next();
	editor.Next();

	CHECK(EditorSurface()->ContentSize.y - withoutPeek == RowsTall(Graphite::kLinePeekRows));
	CHECK(ActiveWindowNamedWith(kPeekWindowMark) != nullptr);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek takes Escape before the find bar")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LinePeekContent loading;

	editor.Item.Find.Open = true;
	ShowPeek(editor, kFirstLine, loading);
	editor.Next();
	editor.Next();

	ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
	editor.Next();

	CHECK(editor.Result.PeekAnswer == Graphite::PeekAction{.Action = Graphite::PeekAction::Kind::Close});
	CHECK(editor.Item.Find.Open);

	ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
	editor.Next();

	CHECK(editor.Item.Find.Open);

	editor.View.Peek.reset();
	editor.Frames.PressKey(ImGuiKey_Escape, [&editor] { editor.Draw(); });

	CHECK_FALSE(editor.Item.Find.Open);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek leaves an Escape taken in front of the editor to that")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LinePeekContent loading;

	editor.Item.Find.Open = true;
	ShowPeek(editor, kFirstLine, loading);
	editor.Next();
	editor.Next();

	// The navigation bubble at the caret took this one.
	editor.View.EscapeTaken = true;
	ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
	editor.Next();

	CHECK_FALSE(editor.Result.PeekAnswer.has_value());
	CHECK(editor.Item.Find.Open);

	ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
	editor.View.EscapeTaken = false;
	editor.Next();

	ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
	editor.Next();

	CHECK(editor.Result.PeekAnswer == Graphite::PeekAction{.Action = Graphite::PeekAction::Kind::Close});
	CHECK(editor.Item.Find.Open);

	ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
	editor.Next();

	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek leaves its line's change mark on the scrollbar one line tall")
{
	constexpr std::size_t kLineCount = 60;
	constexpr std::size_t kChangedLine = 10;
	constexpr float kRowsAtMost = 3.0f;

	EditorFrames editor{LinesOf(kLineCount)};
	editor.View.ChangeDisplay.Gutter = false;

	Graphite::ChangeMarkers changes;
	changes.Lines.resize(BufferOf(editor.Item).GetLineCount());
	changes.Lines[kChangedLine] = Graphite::LineChange{.Kind = Graphite::ChangeKind::Modified};
	changes.Hunks = {Graphite::ChangeHunk{.Kind = Graphite::HunkKind::Modified,
										  .FirstLine = kChangedLine,
										  .LineCount = 1,
										  .MarkedCount = 1}};
	editor.View.Changes = &changes;

	const Graphite::LinePeekContent loading;
	ShowPeek(editor, kChangedLine, loading);
	editor.Next();
	editor.Next();

	ImGuiWindow* const surface = MutableEditorSurface();
	REQUIRE(surface != nullptr);
	REQUIRE(surface->ScrollbarY);

	const ImRect bar = ImGui::GetWindowScrollbarRect(surface, ImGuiAxis_Y);
	const std::optional<VerticalExtent> mark =
		VerticalExtentInColour(*surface->DrawList, DarkEditorPalette().ChangeModified);
	REQUIRE(mark.has_value());

	// The track stands for the document's rows and the peek's; the mark ends with its line, not past the peek.
	const float rowsOnTrack = static_cast<float>(BufferOf(editor.Item).GetLineCount() + Graphite::kLinePeekRows);
	const float rowHeight = bar.GetHeight() / rowsOnTrack;

	CHECK(mark->Bottom - mark->Top < rowHeight * kRowsAtMost);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek reports a selected entry")
{
	constexpr std::size_t kSecondEntry = 1;

	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LinePeekContent history = PeekOfTwoCommits();

	ShowPeek(editor, kFirstLine, history);
	editor.Next();
	editor.Next();

	const std::optional<ImVec2> secondEntry = FindDownTheLeftOf(editor, kPeekListMark, "##LinePeekEntry1");
	REQUIRE(secondEntry.has_value());

	editor.Click(*secondEntry);

	CHECK(editor.Result.PeekAnswer ==
		  Graphite::PeekAction{.Action = Graphite::PeekAction::Kind::Select, .Entry = kSecondEntry});
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek list draws the selected entry's bar where the list shows it")
{
	EditorFrames editor{std::string{kTwoLines}};
	const Graphite::LinePeekContent history = PeekOfTwoCommits();

	ShowPeek(editor, kFirstLine, history);
	editor.Next();
	editor.Next();

	const ImGuiWindow* const list = ActiveWindowNamedWith(kPeekListMark);
	REQUIRE(list != nullptr);

	const std::optional<HorizontalExtent> bar = HorizontalExtentInColour(*list->DrawList, PeekAccent());
	REQUIRE(bar.has_value());

	CHECK(bar->Left >= list->InnerClipRect.Min.x);
	CHECK(bar->Right <= list->InnerClipRect.Max.x);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek offers Show commit beside the byline above a long diff")
{
	constexpr int kDiffLines = 30;
	constexpr float kStep = 4.0f;

	EditorFrames editor{LinesOf(kDiffLines)};

	Graphite::LinePeekContent content;
	content.State = Graphite::PeekState::Ready;
	content.Header = "History of line 1";

	Graphite::PeekEntry entry{.Title = "Add", .MetaName = "Ann", .Byline = "Ann", .Commit = Core::Oid{}};

	// The tracked line is the first, so the detail opens at its top.
	for (int line = 0; line < kDiffLines; ++line)
	{
		entry.Lines.push_back({Core::DiffLineKind::Added, "line", line == 0});
	}

	content.Entries = {entry};

	ShowPeek(editor, kFirstLine, content);
	editor.Next();
	editor.Next();

	ImGuiWindow* const detail = ActiveWindowNamedWith("/##LinePeekDiff_");
	REQUIRE(detail != nullptr);
	REQUIRE(detail->Scroll.y == 0.0f);

	const ImGuiID showCommit = detail->GetID("Show commit");
	const ImRect shown = detail->InnerClipRect;
	bool found = false;

	for (float y = shown.Min.y + kStep; y < shown.Max.y && !found; y += kStep)
	{
		for (float x = shown.Min.x + kStep; x < shown.Max.x && !found; x += kStep)
		{
			editor.HoverAt(ImVec2(x, y));
			found = ImGui::GetCurrentContext()->HoveredId == showCommit;
		}
	}

	CHECK(found);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek detail opens each entry and each peek at its own place")
{
	constexpr std::size_t kDiffLines = 40;
	constexpr std::size_t kFirstDiffLine = 0;
	constexpr std::size_t kFirstEntry = 0;
	constexpr std::size_t kSecondEntry = 1;

	EditorFrames editor{LinesOf(kDiffLines)};

	// Every entry's tracked line is its first, so each opens at its top.
	const Graphite::LinePeekContent first =
		ReadyPeek("History of line 1", {LongEntry("Fix", "line", kDiffLines, kFirstDiffLine),
										LongEntry("Add", "line", kDiffLines, kFirstDiffLine)});
	const Graphite::LinePeekContent second =
		ReadyPeek("History of line 2", {LongEntry("Fix", "line", kDiffLines, kFirstDiffLine),
										LongEntry("Add", "line", kDiffLines, kFirstDiffLine)});

	const auto detailScroll = [] {
		const ImGuiWindow* const detail = ActiveWindowNamedWith(kPeekDetailMark);
		REQUIRE(detail != nullptr);
		return detail->Scroll.y;
	};

	const auto scrollDetailToItsEnd = [&editor, &detailScroll] {
		ImGuiWindow* const detail = ActiveWindowNamedWith(kPeekDetailMark);
		REQUIRE(detail != nullptr);
		ImGui::SetScrollY(detail, detail->ScrollMax.y);
		editor.Next();
		editor.Next();
		REQUIRE(detailScroll() > 0.0f);
	};

	ShowPeek(editor, kFirstLine, first, kJustOpened);
	editor.Next();
	editor.View.Peek->JustOpened = false;
	editor.Next();
	editor.Next();

	scrollDetailToItsEnd();

	editor.View.Peek->Selected = kSecondEntry;
	editor.Next();
	editor.Next();

	CHECK(detailScroll() == 0.0f);

	editor.View.Peek->Selected = kFirstEntry;
	editor.Next();
	editor.Next();

	CHECK(detailScroll() == 0.0f);

	scrollDetailToItsEnd();

	// Another line's peek in the same editor, on the entry of the same number.
	ShowPeek(editor, kSecondLine, second, kJustOpened);
	editor.Next();
	editor.View.Peek->JustOpened = false;
	editor.Next();
	editor.Next();

	CHECK(detailScroll() == 0.0f);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek detail shows the tracked line of an entry when it opens")
{
	constexpr std::size_t kDiffLines = 40;
	constexpr std::size_t kTrackedLine = 25;

	EditorFrames editor{LinesOf(kDiffLines)};
	const Graphite::LinePeekContent content =
		ReadyPeek("History of line 1", {LongEntry("Fix", "line", kDiffLines, kTrackedLine)});

	ShowPeek(editor, kFirstLine, content, kJustOpened);
	editor.Next();
	editor.View.Peek->JustOpened = false;
	editor.Next();
	editor.Next();

	const ImGuiWindow* const detail = ActiveWindowNamedWith(kPeekDetailMark);
	REQUIRE(detail != nullptr);

	const std::optional<VerticalExtent> bar = VerticalExtentInColour(*detail->DrawList, PeekAccent());
	REQUIRE(bar.has_value());

	CHECK(bar->Top >= detail->InnerRect.Min.y - kJustInside);
	CHECK(bar->Bottom <= detail->InnerRect.Max.y + kJustInside);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek detail keeps the tracked line clear of its sideways scrollbar in a narrow editor")
{
	constexpr std::size_t kDiffLines = 40;
	constexpr std::size_t kTrackedLine = 25;
	constexpr float kNarrowWidth = 420.0f;
	constexpr std::string_view kWideLine = "a line of code far wider than the narrow detail beside the list";

	EditorFrames editor{LinesOf(kDiffLines)};
	editor.Width = kNarrowWidth;

	const Graphite::LinePeekContent content =
		ReadyPeek("History of line 1", {LongEntry("Fix", kWideLine, kDiffLines, kTrackedLine)});

	ShowPeek(editor, kFirstLine, content, kJustOpened);
	editor.Next();
	editor.View.Peek->JustOpened = false;
	editor.Next();
	editor.Next();

	const ImGuiWindow* const detail = ActiveWindowNamedWith(kPeekDetailMark);
	REQUIRE(detail != nullptr);
	REQUIRE(detail->ScrollbarX);

	const std::optional<VerticalExtent> bar = VerticalExtentInColour(*detail->DrawList, PeekAccent());
	REQUIRE(bar.has_value());

	CHECK(bar->Top >= detail->InnerRect.Min.y - kJustInside);
	CHECK(bar->Bottom <= detail->InnerRect.Max.y + kJustInside);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek detail keeps Show commit whole in a narrow editor")
{
	constexpr std::size_t kDiffLines = 3;
	constexpr float kNarrowWidth = 420.0f;
	constexpr float kStep = 2.0f;
	constexpr float kBothSides = 2.0f;

	EditorFrames editor{LinesOf(kDiffLines)};
	editor.Width = kNarrowWidth;

	Graphite::PeekEntry entry =
		LongEntry("Iterate a copy so handlers can unsubscribe while publishing", "line", kDiffLines, kFirstLine);
	entry.Byline = "Anna Kowalczyk-Wisniewska \xC2\xB7 2026-07-14 16:42";

	const Graphite::LinePeekContent content = ReadyPeek("History of line 1", {entry});
	float buttonWidth = 0.0f;

	ShowPeek(editor, kFirstLine, content);
	editor.Next();
	editor.Frames.Next([&] {
		editor.Draw();

		Core::PushInterfaceFont();
		buttonWidth = ImGui::CalcTextSize("Show commit").x + ImGui::GetStyle().FramePadding.x * kBothSides;
		ImGui::PopFont();
	});

	ImGuiWindow* const detail = ActiveWindowNamedWith(kPeekDetailMark);
	REQUIRE(detail != nullptr);

	const ImGuiID showCommit = detail->GetID("Show commit");
	const ImRect shown = detail->InnerClipRect;
	float left = FLT_MAX;
	float right = -FLT_MAX;

	for (float y = shown.Min.y + kStep; y < shown.Max.y && left == FLT_MAX; y += kStep)
	{
		for (float x = shown.Min.x + kStep; x < shown.Max.x; x += kStep)
		{
			editor.HoverAt(ImVec2(x, y));

			if (ImGui::GetCurrentContext()->HoveredId == showCommit)
			{
				left = std::min(left, x);
				right = std::max(right, x);
			}
		}
	}

	REQUIRE(left < right);
	CHECK(right - left >= buttonWidth - kStep * kBothSides);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek scrolls into view when it opens")
{
	constexpr std::size_t kLineCount = 60;

	EditorFrames editor{LinesOf(kLineCount)};
	const Graphite::LinePeekContent loading;

	editor.Next();
	editor.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);
	REQUIRE(surface->Scroll.y == 0.0f);

	const auto rowsShown = static_cast<std::size_t>(surface->InnerRect.GetHeight() / kEditorLineHeight);
	REQUIRE(rowsShown > 1);

	ShowPeek(editor, rowsShown - 1, loading, kJustOpened);
	editor.Next();
	editor.View.Peek->JustOpened = false;
	editor.Next();

	const ImGuiWindow* peek = ActiveWindowNamedWith(kPeekWindowMark);
	REQUIRE(peek != nullptr);

	CHECK(surface->Scroll.y > 0.0f);
	CHECK(peek->Rect().Min.y >= surface->InnerRect.Min.y);
	CHECK(peek->Rect().Max.y <= surface->InnerRect.Max.y);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek holds the scroll when only its rows are visible")
{
	constexpr std::size_t kLineCount = 60;
	constexpr std::size_t kPeekLine = 10;
	constexpr std::size_t kRowsShown = 4;
	constexpr std::size_t kPeekRowsAboveTheView = 1;
	constexpr float kTopAndBottom = 2.0f;

	EditorFrames editor{LinesOf(kLineCount)};
	editor.Height = RowsTall(kRowsShown) + ImGui::GetStyle().WindowPadding.y * kTopAndBottom;

	Graphite::ScrollAnchor anchor;
	const Graphite::LinePeekContent loading;

	editor.View.Anchor = &anchor;
	ShowPeek(editor, kPeekLine, loading);
	editor.Next();
	editor.Next();

	ImGuiWindow* const surface = MutableEditorSurface();
	REQUIRE(surface != nullptr);
	REQUIRE(surface->InnerRect.GetHeight() <= RowsTall(kRowsShown));

	// The peek's rows begin right under its line, so this shows none of the document's rows.
	const float intoThePeek = RowsTall(kPeekLine + 1 + kPeekRowsAboveTheView);
	ImGui::SetScrollY(surface, intoThePeek);
	editor.Next();
	editor.Next();

	REQUIRE(surface->Scroll.y == intoThePeek);
	CHECK(anchor.Line == kPeekLine + 1);

	Graphite::LensModel lenses;
	DeclareReferencedFirstLine(lenses, editor.Item);
	editor.View.Lens = &lenses;
	editor.Next();
	editor.Next();

	CHECK(surface->Scroll.y == intoThePeek + kEditorLineHeight);
	CHECK(editor.StackAfter == editor.StackBefore);
}

TEST_CASE("Peek command Line History from the menu opens on the right-clicked line inside a selection")
{
	constexpr std::size_t kThirdLine = 2;
	constexpr std::size_t kIntoTheLine = 2;

	EditorFrames editor{"first\nsecond\nthird\n"};
	Graphite::EditorWidgetState widget;
	Core::CommandRegistry commands;
	Core::Keymap keys;
	Graphite::ActiveEditorTarget active;
	LineHistoryTarget target{editor.Item, widget};
	const std::vector<Core::MenuItem> menu = Graphite::BuildEditorContextMenu();

	Graphite::RegisterEditorNavigationCommands(commands, keys, active, Graphite::EditorCommandServices{});
	active.Set(target);

	editor.View.Widget = &widget;
	editor.Context.Commands = &commands;
	editor.Context.Keys = &keys;
	editor.Context.ContextMenu = &menu;

	editor.Next();
	const ImVec2 firstLine = editor.CaretScreenPos();

	// Selected from the start of the document into the third line, where the caret is.
	editor.Item.Caret = Core::Caret{.Offset = BufferOf(editor.Item).LineStart(kThirdLine) + kIntoTheLine, .Anchor = 0};
	editor.Next();

	editor.Click(ImVec2(firstLine.x + kIntoTheRow, firstLine.y + kEditorLineHeight + kIntoTheRow),
				 ImGuiMouseButton_Right);
	editor.Next();

	REQUIRE(CaretLine(editor.Item) == kThirdLine);
	REQUIRE(editor.Frames.ClickMenuItem("Line History###git.lineHistory", [&editor] { editor.Draw(); }));

	REQUIRE(target.OpenedOn.has_value());
	CHECK(*target.OpenedOn == kSecondLine);
	CHECK(CaretLine(editor.Item) == kSecondLine);
	CHECK_FALSE(Core::HasSelection(editor.Item.Caret));

	editor.Next();

	CHECK_FALSE(widget.ContextMenuOffset.has_value());
	CHECK(editor.StackAfter == editor.StackBefore);
}
