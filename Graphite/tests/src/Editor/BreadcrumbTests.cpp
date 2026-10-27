#include <doctest/doctest.h>

#include "DrawnColours.h"
#include "ImGuiFrames.h"

#include "Editor/Breadcrumb.h"
#include "Editor/Find/FindBar.h"
#include "Editor/TextEditorWidget.h"
#include "UI/FileIcons.h"
#include "UI/TypographicMarks.h"

#include "Core/ImGui/FontSet.h"
#include "Core/ImGui/ImGuiColors.h"
#include "Core/ImGui/UiScale.h"
#include "Core/Text/Caret.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/TextDocument.h"

#include <IconsLucide.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using Graphite::BreadcrumbModel;
using Graphite::BreadcrumbSymbol;
using Graphite::Declaration;
using Graphite::DeclarationKind;
using Graphite::FileKind;

namespace {

	constexpr float kRowHeight = 24.0f;
	constexpr float kRowFontSize = 15.0f;
	constexpr float kMarkFontSize = 15.0f;
	constexpr float kRowLeftPadding = 12.0f;
	constexpr float kChevronAlpha = 0.7f;
	constexpr float kTallTabBarPadding = 10.0f;
	constexpr int kSettleFrames = 3;
	constexpr ImWchar kFunctionMarkCodepoint = 0x0192;
	constexpr std::string_view kFunctionMark = "\xC6\x92";
	constexpr std::string_view kTypeMark = "{}";
	constexpr ImU32 kDarkMarkPurple = IM_COL32(0xB3, 0x92, 0xE8, 0xFF);
	constexpr ImU32 kLightMarkPurple = IM_COL32(0x7A, 0x4F, 0xC4, 0xFF);
	constexpr float kDocumentWidth = 800.0f;
	constexpr float kDocumentHeight = 400.0f;
	constexpr float kNarrowDocumentWidth = 60.0f;
	constexpr float kEditorFontSize = 20.0f;
	constexpr float kHalf = 0.5f;
	constexpr float kHalfPixel = 0.5f;
	constexpr float kOnePixel = 1.0f;
	constexpr float kGlyphBearing = 2.0f;
	constexpr float kIntoTheText = 120.0f;
	constexpr float kIntoTheRow = 40.0f;
	constexpr std::size_t kNoCaretMove = 0;
	constexpr const char* kDocumentWindow = "Document";
	constexpr const char* kDockHostWindow = "##DockHost";
	constexpr const char* kDockSpace = "##DockSpace";
	constexpr const char* kReferenceWindow = "##BreadcrumbReference";
	constexpr std::string_view kEditorSurface = "##EditorSurface";
	constexpr std::string_view kFindBar = "##FindBar";

	constexpr std::size_t kNoSymbolLine = 0;
	constexpr std::size_t kLineInsideTheClass = 3;
	constexpr std::size_t kLineInsideDraw = 6;
	constexpr std::size_t kLineInsideClear = 13;
	constexpr std::size_t kLinePastEverything = 40;

	constexpr int kFirstDeclaration = 0;

	Declaration Spanning(std::string name, DeclarationKind kind, std::size_t line, std::size_t endLine,
						 int parent = Graphite::kNoParentDeclaration)
	{
		Declaration declaration;
		declaration.Name = std::move(name);
		declaration.Kind = kind;
		declaration.Line = line;
		declaration.NameLine = line;
		declaration.EndLine = endLine;
		declaration.Parent = parent;

		return declaration;
	}

	std::vector<Declaration> RendererWithTwoMethods()
	{
		return {Spanning("Renderer", DeclarationKind::Type, 2, 20),
				Spanning("Draw", DeclarationKind::Function, 4, 9, kFirstDeclaration),
				Spanning("Clear", DeclarationKind::Function, 12, 15, kFirstDeclaration)};
	}

	const std::filesystem::path kRoot = "D:/Repos/IDE";
	const std::filesystem::path kNoRoot;

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

	const ImDrawList& DocumentDrawing()
	{
		const ImGuiWindow* document = ImGui::FindWindowByName(kDocumentWindow);
		REQUIRE(document != nullptr);

		return *document->DrawList;
	}

	struct DocumentFrames
	{
		ImGuiFrames Frames;
		Core::OpenItem Item;
		Graphite::EditorView View;
		Graphite::CommandContext Context;
		Graphite::FindBar Bar;
		BreadcrumbModel Model;
		float Width = kDocumentWidth;
		bool WithEditor = true;
		ImVec2 RowStart;
		float WindowLeft = 0.0f;
		float ContentBottom = 0.0f;
		Graphite::BreadcrumbTools Tools;
		ImVec2 CursorAfterTheRow;

		explicit DocumentFrames(BreadcrumbModel model) : Item(ItemWith("first line\nsecond line\nthird line\n")), Model(std::move(model))
		{
			View.FontSize = kEditorFontSize;
		}

		// The row starts inside the window's side padding but at the top of its inner rectangle, with no top padding
		// above it. The window draws no border of its own, as a docked document window does not, so any border drawn
		// in the row is the row's.
		void Draw()
		{
			constexpr float kNoBorder = 0.0f;

			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(Width, kDocumentHeight));
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kNoBorder);
			ImGui::Begin(kDocumentWindow, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
			ImGui::PopStyleVar();

			RowStart = ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCurrentWindow()->InnerRect.Min.y);
			WindowLeft = ImGui::GetWindowPos().x;
			ContentBottom = ImGui::GetCursorScreenPos().y + ImGui::GetContentRegionAvail().y;

			Graphite::DrawBreadcrumb(Model, Tools);
			CursorAfterTheRow = ImGui::GetCursorScreenPos();

			if (WithEditor)
			{
				Graphite::DrawEditor(Context, Item, View, Bar);
			}

			ImGui::End();
		}

		void Next()
		{
			Frames.Next([this] { Draw(); });
		}

		void Click(ImVec2 at)
		{
			Frames.Click(at, [this] { Draw(); });
		}

		ImVec2 RowMiddle() const { return ImVec2(RowStart.x + kIntoTheRow, RowStart.y + Core::Scaled(kRowHeight) * kHalf); }
	};

	// A document window docked alone into a dock space, as a document tab is, so it has a tab bar above its content.
	struct DockedDocumentFrames
	{
		ImGuiFrames Frames;
		Core::OpenItem Item;
		Graphite::EditorView View;
		Graphite::CommandContext Context;
		Graphite::FindBar Bar;
		BreadcrumbModel Model;

		explicit DockedDocumentFrames(BreadcrumbModel model) : Item(ItemWith("first line\nsecond line\n")), Model(std::move(model))
		{
			ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
			View.FontSize = kEditorFontSize;
		}

		void Draw()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(kDocumentWidth, kDocumentHeight));
			ImGui::Begin(kDockHostWindow, nullptr,
						 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);

			const ImGuiID dockSpace = ImGui::GetID(kDockSpace);

			if (ImGui::DockBuilderGetNode(dockSpace) == nullptr)
			{
				ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
				ImGui::DockBuilderSetNodeSize(dockSpace, ImGui::GetContentRegionAvail());
				ImGui::DockBuilderDockWindow(kDocumentWindow, dockSpace);
				ImGui::DockBuilderFinish(dockSpace);
			}

			ImGui::DockSpace(dockSpace);
			ImGui::End();

			ImGui::Begin(kDocumentWindow);
			Graphite::DrawBreadcrumb(Model);
			Graphite::DrawEditor(Context, Item, View, Bar);
			ImGui::End();
		}

		void Settle()
		{
			for (int frame = 0; frame < kSettleFrames; ++frame)
			{
				Frames.Next([this] { Draw(); });
			}
		}

		static float TabBarBottom()
		{
			const ImGuiWindow* document = ImGui::FindWindowByName(kDocumentWindow);
			REQUIRE(document != nullptr);
			REQUIRE(document->DockIsActive);
			REQUIRE(document->DockNode != nullptr);
			REQUIRE(document->DockNode->TabBar != nullptr);

			return document->DockNode->TabBar->BarRect.Max.y;
		}
	};

	// An ImGui context with no frame, for reading the faces the application loads.
	struct FontContext
	{
		FontContext() { ImGui::SetCurrentContext(Context); }
		~FontContext()
		{
			ImGui::DestroyContext(Context);
			ImGui::SetCurrentContext(Previous);
		}

		FontContext(const FontContext&) = delete;
		FontContext& operator=(const FontContext&) = delete;

		ImGuiContext* Previous = ImGui::GetCurrentContext();
		ImGuiContext* Context = ImGui::CreateContext();
	};

	std::optional<HorizontalExtent> ReferenceTextExtent(const char* window, const char* text, float size)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kDocumentWidth, kDocumentHeight));
		ImGui::Begin(window, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

		Core::PushInterfaceFont(size);
		ImGui::TextUnformatted(text);
		ImGui::PopFont();

		const std::optional<HorizontalExtent> extent =
			HorizontalExtentInColour(*ImGui::GetWindowDrawList(), ImGui::GetColorU32(ImGuiCol_Text));
		ImGui::End();

		return extent;
	}

	struct ReferenceInk
	{
		float LineHeight = 0.0f;
		float InkTopBelowLineTop = 0.0f;
	};

	ReferenceInk ReferenceInkOf(const char* text)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kDocumentWidth, kDocumentHeight));
		ImGui::Begin(kReferenceWindow, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

		Core::PushInterfaceFont(kRowFontSize);
		const float lineTop = ImGui::GetCursorScreenPos().y;
		ReferenceInk ink{.LineHeight = ImGui::GetTextLineHeight()};
		ImGui::TextUnformatted(text);
		ImGui::PopFont();

		const std::optional<VerticalExtent> drawn =
			VerticalExtentInColour(*ImGui::GetWindowDrawList(), ImGui::GetColorU32(ImGuiCol_Text));
		REQUIRE(drawn.has_value());
		ink.InkTopBelowLineTop = drawn->Top - lineTop;
		ImGui::End();

		return ink;
	}

	struct GlyphQuad
	{
		ImRect Box;
		ImVec2 Uv;
	};

	bool IsGlyphVertex(const ImDrawVert& vertex)
	{
		const ImVec2 white = ImGui::GetDrawListSharedData()->TexUvWhitePixel;
		return vertex.uv.x != white.x || vertex.uv.y != white.y;
	}

	std::vector<GlyphQuad> GlyphQuadsInColour(const ImDrawList& drawing, ImU32 colour)
	{
		constexpr int kVerticesInAGlyph = 4;

		std::vector<GlyphQuad> quads;
		int first = 0;

		while (first + kVerticesInAGlyph <= drawing.VtxBuffer.Size)
		{
			const ImDrawVert* quad = &drawing.VtxBuffer[first];
			bool glyphInColour = true;

			for (int corner = 0; corner < kVerticesInAGlyph; ++corner)
			{
				glyphInColour = glyphInColour && quad[corner].col == colour && IsGlyphVertex(quad[corner]);
			}

			if (glyphInColour)
			{
				quads.push_back(GlyphQuad{.Box = ImRect(quad[0].pos, quad[2].pos), .Uv = quad[0].uv});
				first += kVerticesInAGlyph;
				continue;
			}

			++first;
		}

		return quads;
	}

	bool Near(float value, float expected)
	{
		constexpr float kSameSpot = 0.01f;

		return std::fabs(value - expected) < kSameSpot;
	}

	bool SameGlyph(const GlyphQuad& drawn, const GlyphQuad& reference)
	{
		return Near(drawn.Uv.x, reference.Uv.x) && Near(drawn.Uv.y, reference.Uv.y) &&
			   Near(drawn.Box.GetWidth(), reference.Box.GetWidth()) && Near(drawn.Box.GetHeight(), reference.Box.GetHeight());
	}

	bool SameRun(const std::vector<GlyphQuad>& drawn, const std::vector<GlyphQuad>& reference)
	{
		if (drawn.size() != reference.size() || reference.empty())
		{
			return false;
		}

		for (std::size_t glyph = 0; glyph < reference.size(); ++glyph)
		{
			const bool sameSpot = Near(drawn[glyph].Box.Min.x - drawn.front().Box.Min.x,
									   reference[glyph].Box.Min.x - reference.front().Box.Min.x);

			if (!SameGlyph(drawn[glyph], reference[glyph]) || !sameSpot)
			{
				return false;
			}
		}

		return true;
	}

	enum class Face
	{
		Interface,
		Code
	};

	std::vector<GlyphQuad> ReferenceGlyphs(const char* window, std::string_view text, Face face = Face::Interface)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(kDocumentWidth, kDocumentHeight));
		ImGui::Begin(window, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

		if (face == Face::Code)
		{
			Core::PushCodeFont(kMarkFontSize);
		}
		else
		{
			Core::PushInterfaceFont(kRowFontSize);
		}

		ImGui::TextUnformatted(text.data(), text.data() + text.size());
		ImGui::PopFont();

		std::vector<GlyphQuad> glyphs = GlyphQuadsInColour(*ImGui::GetWindowDrawList(), ImGui::GetColorU32(ImGuiCol_Text));
		ImGui::End();

		return glyphs;
	}

	BreadcrumbModel FileOnly(std::string fileName, FileKind kind)
	{
		BreadcrumbModel model;
		model.FileName = std::move(fileName);
		model.Kind = kind;

		return model;
	}
}

TEST_CASE("A file under the root shows the folders between them, its name and its kind")
{
	const BreadcrumbModel model =
		Graphite::BuildBreadcrumb(kRoot, kRoot / "Graphite/src/App/CommandLine.cpp", {}, kNoSymbolLine);

	CHECK(model.Folders == std::vector<std::string>{"Graphite", "src", "App"});
	CHECK(model.FileName == "CommandLine.cpp");
	CHECK(model.Kind == FileKind::CppSource);
	CHECK(model.Symbols.empty());
}

TEST_CASE("A file at the root shows no folder")
{
	const BreadcrumbModel model = Graphite::BuildBreadcrumb(kRoot, kRoot / "premake5.lua", {}, kNoSymbolLine);

	CHECK(model.Folders.empty());
	CHECK(model.FileName == "premake5.lua");
	CHECK(model.Kind == FileKind::Lua);
}

TEST_CASE("A file outside the root shows its absolute folders, its drive first")
{
	const BreadcrumbModel model = Graphite::BuildBreadcrumb(kRoot, "D:/Temp/file.cpp", {}, kNoSymbolLine);

	CHECK(model.Folders == std::vector<std::string>{"D:", "Temp"});
	CHECK(model.FileName == "file.cpp");
}

TEST_CASE("With no folder open a file shows its absolute folders")
{
	const BreadcrumbModel model = Graphite::BuildBreadcrumb(kNoRoot, "D:/Temp/notes/file.json", {}, kNoSymbolLine);

	CHECK(model.Folders == std::vector<std::string>{"D:", "Temp", "notes"});
	CHECK(model.Kind == FileKind::Json);
}

TEST_CASE("A file under a drive's root shows the folders below the drive")
{
	const std::filesystem::path driveRoot = "D:\\";

	const BreadcrumbModel nested = Graphite::BuildBreadcrumb(driveRoot, "D:\\Temp\\notes\\file.cpp", {}, kNoSymbolLine);
	CHECK(nested.Folders == std::vector<std::string>{"Temp", "notes"});
	CHECK(nested.FileName == "file.cpp");

	CHECK(Graphite::BuildBreadcrumb(driveRoot, "D:\\file.cpp", {}, kNoSymbolLine).Folders.empty());
}

TEST_CASE("A root spelt in another case still holds the file, and the folders keep the file's own spelling")
{
	const BreadcrumbModel model =
		Graphite::BuildBreadcrumb("d:/repos/ide", "D:/Repos/IDE/Graphite/src/App/CommandLine.cpp", {}, kNoSymbolLine);

	CHECK(model.Folders == std::vector<std::string>{"Graphite", "src", "App"});
	CHECK(model.FileName == "CommandLine.cpp");
}

TEST_CASE("A document without symbols at the caret shows the path alone")
{
	const std::vector<Declaration> declarations = RendererWithTwoMethods();

	CHECK(Graphite::BuildBreadcrumb(kRoot, kRoot / "src/Renderer.cpp", {}, kLineInsideDraw).Symbols.empty());
	CHECK(Graphite::BuildBreadcrumb(kRoot, kRoot / "src/Renderer.cpp", declarations, kLinePastEverything).Symbols.empty());
}

TEST_CASE("The caret inside a method shows its class and then the method, each with its kind")
{
	const std::vector<Declaration> declarations = RendererWithTwoMethods();
	const std::filesystem::path path = kRoot / "src/Renderer.cpp";

	CHECK(Graphite::BuildBreadcrumb(kRoot, path, declarations, kLineInsideDraw).Symbols ==
		  std::vector<BreadcrumbSymbol>{{.Name = "Renderer", .Kind = DeclarationKind::Type},
										{.Name = "Draw", .Kind = DeclarationKind::Function}});
	CHECK(Graphite::BuildBreadcrumb(kRoot, path, declarations, kLineInsideClear).Symbols ==
		  std::vector<BreadcrumbSymbol>{{.Name = "Renderer", .Kind = DeclarationKind::Type},
										{.Name = "Clear", .Kind = DeclarationKind::Function}});
	CHECK(Graphite::BuildBreadcrumb(kRoot, path, declarations, kLineInsideTheClass).Symbols ==
		  std::vector<BreadcrumbSymbol>{{.Name = "Renderer", .Kind = DeclarationKind::Type}});
}

TEST_CASE("The breadcrumb is one row of 24 px above the code, at the top of the window's content")
{
	DocumentFrames document(FileOnly("CommandLine.cpp", FileKind::CppSource));

	ReferenceInk reference;
	ImU32 kind = 0;
	document.Frames.Next([&] {
		document.Draw();
		reference = ReferenceInkOf("CommandLine.cpp");
		kind = Graphite::ColourOf(FileKind::CppSource, Graphite::IsLightScheme());
	});
	document.Next();

	const float rowHeight = Core::Scaled(kRowHeight);
	CHECK(Graphite::BreadcrumbHeight() == doctest::Approx(rowHeight));

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);
	CHECK(surface->Pos.y == doctest::Approx(document.RowStart.y + rowHeight));
	CHECK(surface->Pos.y + surface->Size.y == doctest::Approx(document.ContentBottom));

	const std::optional<VerticalExtent> name =
		VerticalExtentInColour(DocumentDrawing(), ImGui::GetColorU32(ImGuiCol_Text));
	REQUIRE(name.has_value());
	CHECK(name->Top >= document.RowStart.y);
	CHECK(name->Bottom <= document.RowStart.y + rowHeight);

	const float lineTop = name->Top - reference.InkTopBelowLineTop;
	const float centredLineTop = document.RowStart.y + (rowHeight - reference.LineHeight) * kHalf;
	CHECK(std::fabs(lineTop - centredLineTop) <= kHalfPixel);

	REQUIRE(document.RowStart.x > document.WindowLeft + kOnePixel);

	const std::optional<HorizontalExtent> icon = HorizontalExtentInColour(DocumentDrawing(), kind);
	REQUIRE(icon.has_value());

	const float firstPieceLeft = document.WindowLeft + Core::Scaled(kRowLeftPadding);
	CHECK(icon->Left >= firstPieceLeft - kOnePixel);
	CHECK(icon->Left <= firstPieceLeft + kGlyphBearing);
}

TEST_CASE("The breadcrumb's top is the tab bar's bottom")
{
	DockedDocumentFrames document(FileOnly("CommandLine.cpp", FileKind::CppSource));

	SUBCASE("under the tab bar the style gives")
	{
	}

	SUBCASE("under a taller tab bar")
	{
		ImGui::GetStyle().FramePadding.y = kTallTabBarPadding;
	}

	document.Settle();

	ReferenceInk reference;
	document.Frames.Next([&] {
		document.Draw();
		reference = ReferenceInkOf("CommandLine.cpp");
	});

	const float tabBarBottom = DockedDocumentFrames::TabBarBottom();
	const float rowHeight = Core::Scaled(kRowHeight);

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);
	CHECK(surface->Pos.y == doctest::Approx(tabBarBottom + rowHeight));

	const std::optional<VerticalExtent> name =
		VerticalExtentInColour(DocumentDrawing(), ImGui::GetColorU32(ImGuiCol_Text));
	REQUIRE(name.has_value());

	const float lineTop = name->Top - reference.InkTopBelowLineTop;
	const float centredLineTop = tabBarBottom + (rowHeight - reference.LineHeight) * kHalf;
	CHECK(std::fabs(lineTop - centredLineTop) <= kHalfPixel);
}

TEST_CASE("Under the tab bar an open find bar still stands inside the editor surface, below the breadcrumb")
{
	DockedDocumentFrames document(FileOnly("CommandLine.cpp", FileKind::CppSource));
	document.Item.Find.Open = true;
	document.Settle();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	const ImGuiWindow* findBar = nullptr;
	for (const ImGuiWindow* child : surface->DC.ChildWindows)
	{
		findBar = std::string_view{child->Name}.find(kFindBar) != std::string_view::npos ? child : findBar;
	}
	REQUIRE(findBar != nullptr);

	CHECK(surface->Pos.y == doctest::Approx(DockedDocumentFrames::TabBarBottom() + Core::Scaled(kRowHeight)));
	CHECK(findBar->Pos.y > surface->Pos.y);
	CHECK(findBar->Pos.y + findBar->Size.y < surface->Pos.y + surface->Size.y);
}

TEST_CASE("The breadcrumb draws in the interface face at 15 px")
{
	constexpr const char* kName = "CommandLine.cpp";

	DocumentFrames document(FileOnly(kName, FileKind::CppSource));
	document.WithEditor = false;

	std::optional<HorizontalExtent> atTheRowSize;
	std::optional<HorizontalExtent> atTheInterfaceSize;
	document.Frames.Next([&] {
		document.Draw();
		atTheRowSize = ReferenceTextExtent("##AtTheRowSize", kName, kRowFontSize);
		atTheInterfaceSize = ReferenceTextExtent("##AtTheInterfaceSize", kName, Core::kInterfaceFontSize);
	});

	REQUIRE(atTheRowSize.has_value());
	REQUIRE(atTheInterfaceSize.has_value());
	REQUIRE(atTheRowSize->Right - atTheRowSize->Left < atTheInterfaceSize->Right - atTheInterfaceSize->Left - kOnePixel);

	const std::optional<HorizontalExtent> drawn =
		HorizontalExtentInColour(DocumentDrawing(), ImGui::GetColorU32(ImGuiCol_Text));
	REQUIRE(drawn.has_value());
	CHECK(drawn->Right - drawn->Left == doctest::Approx(atTheRowSize->Right - atTheRowSize->Left));
}

TEST_CASE("Folders are dim, and the file name follows its kind's badge in the kind's colour")
{
	BreadcrumbModel model = FileOnly("data.json", FileKind::Json);
	model.Folders = {"src"};
	model.Symbols = {{.Name = "Widget", .Kind = DeclarationKind::Type}, {.Name = "Draw", .Kind = DeclarationKind::Function}};

	DocumentFrames document(std::move(model));
	document.WithEditor = false;

	ImU32 dim = 0;
	ImU32 text = 0;
	ImU32 kind = 0;
	ImU32 border = 0;
	document.Frames.Next([&] {
		document.Draw();
		dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		text = ImGui::GetColorU32(ImGuiCol_Text);
		kind = Graphite::ColourOf(FileKind::Json, Graphite::IsLightScheme());
		border = ImGui::GetColorU32(ImGuiCol_Border);
	});

	const std::optional<HorizontalExtent> folder = HorizontalExtentInColour(DocumentDrawing(), dim);
	const std::optional<HorizontalExtent> icon = HorizontalExtentInColour(DocumentDrawing(), kind);
	const std::optional<HorizontalExtent> names = HorizontalExtentInColour(DocumentDrawing(), text);
	REQUIRE(folder.has_value());
	REQUIRE(icon.has_value());
	REQUIRE(names.has_value());

	const float firstPieceLeft = document.WindowLeft + Core::Scaled(kRowLeftPadding);
	CHECK(folder->Left >= firstPieceLeft - kOnePixel);
	CHECK(folder->Left <= firstPieceLeft + kGlyphBearing);
	CHECK(folder->Right < icon->Left);
	CHECK(icon->Right <= names->Left + kOnePixel);

	int borderInsideTheRow = 0;
	for (const ImDrawVert& vertex : DocumentDrawing().VtxBuffer)
	{
		const bool insideTheRow = vertex.pos.x > document.RowStart.x && vertex.pos.y > document.RowStart.y &&
								  vertex.pos.y < document.RowStart.y + Core::Scaled(kRowHeight);
		borderInsideTheRow += vertex.col == border && insideTheRow ? 1 : 0;
	}
	CHECK(borderInsideTheRow == 0);
}

TEST_CASE("Breadcrumb chevrons are drawn at seventy percent alpha")
{
	constexpr std::size_t kChevronsBetweenThreePieces = 2;
	constexpr std::size_t kGlyphsInTheFolders = 6;

	BreadcrumbModel model = FileOnly("CommandLine.cpp", FileKind::CppSource);
	model.Folders = {"src", "App"};

	DocumentFrames document(std::move(model));
	document.WithEditor = false;

	std::vector<GlyphQuad> chevron;
	ImU32 dim = 0;
	document.Frames.Next([&] {
		document.Draw();
		chevron = ReferenceGlyphs("##ChevronReference", ICON_LC_CHEVRON_RIGHT);
		dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	});

	REQUIRE(chevron.size() == 1);

	const std::vector<GlyphQuad> faded = GlyphQuadsInColour(DocumentDrawing(), Core::WithAlphaScaled(dim, kChevronAlpha));
	REQUIRE(faded.size() == kChevronsBetweenThreePieces);

	for (const GlyphQuad& drawn : faded)
	{
		CHECK(SameGlyph(drawn, chevron.front()));
	}

	CHECK(GlyphQuadsInColour(DocumentDrawing(), dim).size() == kGlyphsInTheFolders);
}

TEST_CASE("A function's mark is a purple ƒ")
{
	BreadcrumbModel model = FileOnly("Renderer.cpp", FileKind::CppSource);
	model.Symbols = {{.Name = "Renderer", .Kind = DeclarationKind::Type}, {.Name = "Draw", .Kind = DeclarationKind::Function}};

	DocumentFrames document(std::move(model));
	document.WithEditor = false;

	std::vector<GlyphQuad> typeMark;
	std::vector<GlyphQuad> functionMark;
	ImU32 dim = 0;
	document.Frames.Next([&] {
		document.Draw();
		typeMark = ReferenceGlyphs("##TypeMarkReference", kTypeMark, Face::Code);
		functionMark = ReferenceGlyphs("##FunctionMarkReference", kFunctionMark, Face::Code);
		dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	});

	REQUIRE(typeMark.size() == 2);
	REQUIRE(functionMark.size() == 1);

	const std::vector<GlyphQuad> purple = GlyphQuadsInColour(DocumentDrawing(), kDarkMarkPurple);
	REQUIRE(purple.size() == typeMark.size() + functionMark.size());

	CHECK(SameRun({purple[0], purple[1]}, typeMark));
	CHECK(SameGlyph(purple[2], functionMark.front()));
	CHECK(purple[1].Box.Max.x < purple[2].Box.Min.x);

	CHECK(GlyphQuadsInColour(DocumentDrawing(), dim).empty());
}

TEST_CASE("In a light scheme the symbol marks are a deeper purple")
{
	constexpr std::size_t kGlyphsInBothMarks = 3;

	BreadcrumbModel model = FileOnly("Renderer.cpp", FileKind::CppSource);
	model.Symbols = {{.Name = "Renderer", .Kind = DeclarationKind::Type}, {.Name = "Draw", .Kind = DeclarationKind::Function}};

	DocumentFrames document(std::move(model));
	document.WithEditor = false;

	document.Frames.Next([&] {
		ImGui::StyleColorsLight();
		document.Draw();
	});

	CHECK(GlyphQuadsInColour(DocumentDrawing(), kLightMarkPurple).size() == kGlyphsInBothMarks);
	CHECK(GlyphQuadsInColour(DocumentDrawing(), kDarkMarkPurple).empty());
}

TEST_CASE("The code face holds ƒ, so a function's mark never draws as the missing glyph")
{
	FontContext context;
	const Core::FontSet set = Core::LoadFontSet(*ImGui::GetIO().Fonts);
	REQUIRE(set.Code != nullptr);

	ImFontBaked* const baked = set.Code->GetFontBaked(kMarkFontSize);
	CHECK(baked->FindGlyphNoFallback(kFunctionMarkCodepoint) != nullptr);
}

TEST_CASE("A narrow row drops its leading folders behind one ellipsis and keeps the file and its symbols")
{
	BreadcrumbModel model = FileOnly("CommandLine.cpp", FileKind::CppSource);
	model.Folders = {"Graphite", "src", "App"};
	model.Symbols = {{.Name = "ParseCommandLine", .Kind = DeclarationKind::Function}};

	ImGuiFrames frames;
	frames.Next([&] {
		const float whole = Graphite::BreadcrumbWidth(model, 0);
		const float withoutTheFirst = Graphite::BreadcrumbWidth(model, 1);
		const float fileAndSymbols = Graphite::BreadcrumbWidth(model, model.Folders.size());

		CHECK(withoutTheFirst < whole);
		CHECK(fileAndSymbols < withoutTheFirst);
		CHECK(fileAndSymbols > 0.0f);

		CHECK(Graphite::FirstShownFolder(model, whole) == 0);
		CHECK(Graphite::FirstShownFolder(model, whole - kOnePixel) == 1);
		CHECK(Graphite::FirstShownFolder(model, withoutTheFirst) == 1);
		CHECK(Graphite::FirstShownFolder(model, Graphite::BreadcrumbWidth(model, 2)) == 2);
		CHECK(Graphite::FirstShownFolder(model, fileAndSymbols) == model.Folders.size());
		CHECK(Graphite::FirstShownFolder(model, 0.0f) == model.Folders.size());
	});
}

TEST_CASE("A narrow row draws one ellipsis for the folders it drops and still draws the file name")
{
	constexpr const char* kName = "CommandLine.cpp";
	constexpr std::size_t kLastFolder = 2;
	constexpr std::size_t kEllipsisGlyphs = 1;
	constexpr std::size_t kChevronsBetweenThreePieces = 2;
	constexpr std::size_t kGlyphsInTheLastFolder = 3;
	constexpr float kBothSides = 2.0f;

	BreadcrumbModel model = FileOnly(kName, FileKind::CppSource);
	model.Folders = {"Graphite", "src", "App"};

	DocumentFrames document(model);
	document.WithEditor = false;

	float lastFolderAndFile = 0.0f;
	document.Frames.Next([&] {
		document.Draw();
		lastFolderAndFile = Graphite::BreadcrumbWidth(model, kLastFolder);
	});

	document.Width = std::ceil(lastFolderAndFile) + Core::Scaled(kRowLeftPadding) * kBothSides + kOnePixel;

	std::vector<GlyphQuad> ellipsis;
	std::vector<GlyphQuad> name;
	ImU32 dim = 0;
	ImU32 text = 0;
	document.Frames.Next([&] {
		document.Draw();
		ellipsis = ReferenceGlyphs("##EllipsisReference", Graphite::kEllipsis);
		name = ReferenceGlyphs("##NameReference", kName);
		dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
		text = ImGui::GetColorU32(ImGuiCol_Text);
	});

	REQUIRE(ellipsis.size() == kEllipsisGlyphs);

	const std::vector<GlyphQuad> dimmed = GlyphQuadsInColour(DocumentDrawing(), dim);
	REQUIRE_FALSE(dimmed.empty());
	CHECK(dimmed.size() == kEllipsisGlyphs + kGlyphsInTheLastFolder);
	CHECK(GlyphQuadsInColour(DocumentDrawing(), Core::WithAlphaScaled(dim, kChevronAlpha)).size() ==
		  kChevronsBetweenThreePieces);
	CHECK(SameGlyph(dimmed.front(), ellipsis.front()));

	const float firstPieceLeft = document.WindowLeft + Core::Scaled(kRowLeftPadding);
	CHECK(dimmed.front().Box.Min.x >= firstPieceLeft - kOnePixel);
	CHECK(dimmed.front().Box.Min.x <= firstPieceLeft + kGlyphBearing);

	CHECK(SameRun(GlyphQuadsInColour(DocumentDrawing(), text), name));
}

TEST_CASE("A row too narrow for its file name completes its frames without an ImGui assertion")
{
	BreadcrumbModel model = FileOnly("CommandLine.cpp", FileKind::CppSource);
	model.Folders = {"Graphite", "src", "App"};
	model.Symbols = {{.Name = "ParseCommandLine", .Kind = DeclarationKind::Function}};

	DocumentFrames document(std::move(model));
	document.Width = kNarrowDocumentWidth;

	document.Next();
	document.Next();

	CHECK(EditorSurface() != nullptr);
}

TEST_CASE("A click on the breadcrumb leaves the caret where it was and the focus where it is")
{
	DocumentFrames document(FileOnly("widget.txt", FileKind::Other));
	document.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);

	document.Click(ImVec2(surface->Pos.x + kIntoTheText, surface->Pos.y + kEditorFontSize));
	const std::size_t caret = document.Item.Caret.Offset;
	REQUIRE(caret != kNoCaretMove);
	REQUIRE(ImGui::GetCurrentContext()->NavWindow == surface);

	document.Click(document.RowMiddle());
	document.Next();

	CHECK(document.Item.Caret.Offset == caret);
	CHECK(ImGui::GetCurrentContext()->NavWindow == surface);
}

TEST_CASE("A path row with tools gives up folders to make room for them")
{
	constexpr float kToolsWidth = 200.0f;
	constexpr std::size_t kLastFolder = 2;
	constexpr float kBothSides = 2.0f;

	BreadcrumbModel model = FileOnly("CommandLine.cpp", FileKind::CppSource);
	model.Folders = {"Graphite", "src", "App"};

	DocumentFrames document(model);
	document.WithEditor = false;

	float lastFolderAndFile = 0.0f;
	document.Frames.Next([&] {
		document.Draw();
		lastFolderAndFile = Graphite::BreadcrumbWidth(model, kLastFolder);
	});

	document.Width = std::ceil(lastFolderAndFile) + Core::Scaled(kRowLeftPadding) * kBothSides + kOnePixel;

	ImU32 dim = 0;
	document.Frames.Next([&] {
		document.Draw();
		dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	});
	const std::size_t withoutTools = GlyphQuadsInColour(DocumentDrawing(), dim).size();

	document.Tools.Width = kToolsWidth;
	document.Tools.Draw = [] {};
	document.Frames.Next([&] { document.Draw(); });
	const std::size_t withTools = GlyphQuadsInColour(DocumentDrawing(), dim).size();

	CHECK(withoutTools > 0);
	CHECK(withTools < withoutTools);
}

TEST_CASE("A path row draws its tools at its right end")
{
	constexpr float kToolsWidth = 100.0f;

	DocumentFrames document(FileOnly("widget.txt", FileKind::Other));
	document.WithEditor = false;
	document.Tools.Width = kToolsWidth;

	ImVec2 at;
	int calls = 0;
	document.Tools.Draw = [&] {
		at = ImGui::GetCursorScreenPos();
		++calls;
	};
	document.Next();

	const ImGuiWindow* window = ImGui::FindWindowByName(kDocumentWindow);
	REQUIRE(window != nullptr);

	CHECK(calls == 1);
	CHECK(at.x == doctest::Approx(window->InnerRect.Max.x - Core::Scaled(kRowLeftPadding) - kToolsWidth));
	CHECK(at.y == doctest::Approx(document.RowStart.y));
	CHECK(document.CursorAfterTheRow.y == doctest::Approx(document.RowStart.y + Core::Scaled(kRowHeight)));
}

TEST_CASE("A button among the path row's tools takes a click")
{
	constexpr float kToolsWidth = 100.0f;

	DocumentFrames document(FileOnly("widget.txt", FileKind::Other));
	document.WithEditor = false;
	document.Tools.Width = kToolsWidth;

	bool pressed = false;
	document.Tools.Draw = [&] {
		pressed = ImGui::InvisibleButton("##Tool", ImVec2(kToolsWidth, Core::Scaled(kRowHeight))) || pressed;
	};
	document.Next();

	const ImGuiWindow* window = ImGui::FindWindowByName(kDocumentWindow);
	REQUIRE(window != nullptr);

	document.Click(ImVec2(window->InnerRect.Max.x - Core::Scaled(kRowLeftPadding) - kToolsWidth * kHalf,
						  document.RowStart.y + Core::Scaled(kRowHeight) * kHalf));

	CHECK(pressed);
}

TEST_CASE("A path row without tools draws as before")
{
	DocumentFrames document(FileOnly("widget.txt", FileKind::Other));

	document.Next();
	document.Next();

	const ImGuiWindow* surface = EditorSurface();
	REQUIRE(surface != nullptr);
	CHECK(document.CursorAfterTheRow.y == doctest::Approx(document.RowStart.y + Core::Scaled(kRowHeight)));
	CHECK(surface->Pos.y == doctest::Approx(document.RowStart.y + Core::Scaled(kRowHeight)));
}
