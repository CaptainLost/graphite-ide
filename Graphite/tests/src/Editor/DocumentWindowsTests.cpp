#include <doctest/doctest.h>

#include "Editor/DocumentWindows.h"
#include "UI/FileIcons.h"

#include "Editor/EditorCommandTarget.h"
#include "Editor/Overlays/AnalysisCache.h"
#include "Editor/Server/DocumentServerPort.h"
#include "Editor/Server/PreviewSemantics.h"
#include "Git/Changes/ChangeBases.h"
#include "Git/Changes/ChangeMarkersModel.h"
#include "Language/Analysis/AnalysisStore.h"
#include "Language/Analysis/FileTextSource.h"
#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Diagnostics/DiagnosticsModel.h"
#include "Editor/Navigation/SelectInDocument.h"
#include "Persistence/CaretRestore.h"
#include "Persistence/PersistedState.h"
#include "Search/ReferencesPanel.h"
#include "Theme/ActiveScheme.h"
#include "Theme/BuiltInSchemes.h"
#include "UI/Dialogs/ModalGate.h"
#include "Workspace/Workspace.h"

#include "DeferredJobs.h"
#include "FileFixtures.h"
#include "ImGuiFrames.h"
#include "TempAppSettings.h"
#include "TempDirectory.h"

#include "Core/App/Redraw.h"
#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/ImGui/TabCloseRequests.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"
#include "Core/Text/RowMap.h"

#include <imgui.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

	class SilentServer final : public Graphite::DocumentServerPort
	{
	public:
		void EnsureStarted(const std::filesystem::path&, Graphite::LanguageId) override {}
		void OpenDocument(const std::filesystem::path&, Graphite::LanguageId, std::string) override {}
		void ChangeDocument(const std::filesystem::path&, Graphite::LanguageId, std::string) override {}
		void SaveDocument(const std::filesystem::path&, Graphite::LanguageId) override {}
		void CloseDocument(const std::filesystem::path&, Graphite::LanguageId) override {}
		Graphite::ServerStatus StatusFor(const std::filesystem::path&, Graphite::LanguageId) override { return {}; }
		bool HasChosenServer(Graphite::LanguageId) override { return false; }
		bool OffersSwitchSourceHeader(Graphite::LanguageId) override { return false; }
		Graphite::LanguageServices::Ticket AskDefinition(const std::filesystem::path&, Graphite::LanguageId, const Graphite::FilePosition&) override { return {}; }
		Graphite::LanguageServices::Ticket AskReferences(const std::filesystem::path&, Graphite::LanguageId, const Graphite::FilePosition&) override { return {}; }
		Graphite::LanguageServices::Ticket AskHover(const std::filesystem::path&, Graphite::LanguageId, const Graphite::FilePosition&) override { return {}; }
		Graphite::LanguageServices::Ticket AskSwitchSourceHeader(const std::filesystem::path&, Graphite::LanguageId) override { return {}; }
		Graphite::LanguageServices::Ticket AskSymbols(const std::filesystem::path&, Graphite::LanguageId) override { return {}; }
		Graphite::LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path&, Graphite::LanguageId) override { return {}; }
		void Cancel(Graphite::LanguageServices::Ticket) override {}
		void TakeAnswers(std::vector<Graphite::LanguageServices::Answer>&) override {}
		void TakeRecolour(std::vector<std::filesystem::path>&) override {}
	};

	class SilentPreviewServer final : public Graphite::PreviewServerPort
	{
	public:
		void Open(const std::filesystem::path&, Graphite::LanguageId, std::string) override {}
		Graphite::LanguageServices::Ticket Ask(const std::filesystem::path&, Graphite::LanguageId) override { return {}; }
		void Close(const std::filesystem::path&, Graphite::LanguageId) override {}
	};

	class SilentAnalysisServer final : public Graphite::WorkspaceAnalysisPort
	{
	public:
		void Open(const std::filesystem::path&, Graphite::LanguageId, std::string) override {}
		Graphite::LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path&, Graphite::LanguageId) override { return {}; }
		Graphite::LanguageServices::Ticket AskSymbols(const std::filesystem::path&, Graphite::LanguageId) override { return {}; }
		void Close(const std::filesystem::path&, Graphite::LanguageId) override {}
		Graphite::ServerReadiness ReadinessOf(const std::filesystem::path&, Graphite::LanguageId) override { return Graphite::ServerReadiness::CannotRun; }
		bool HasChosenServer(Graphite::LanguageId) override { return false; }
		std::string ServerNameOf(Graphite::LanguageId) override { return {}; }
	};

	class NoFileTexts final : public Graphite::FileTextSource
	{
	public:
		void Request(const std::filesystem::path&) override {}
		void Take(std::vector<Graphite::FileText>&) override {}
	};

	class NoChangeBases final : public Graphite::ChangeBasePort
	{
	public:
		Core::Result<Graphite::ChangeBases> ReadBases(const std::filesystem::path&) override
		{
			return Core::Error{.Code = Core::ErrorCode::NotFound};
		}
	};

	constexpr std::size_t kAnalysisBudgetBytes = 1024 * 1024;

	constexpr float kEditorFontSize = 16.0f;
	const ImVec2 kDocumentWindowSize{900.0f, 600.0f};

	struct DocumentWindowsWithHeldReads
	{
		ImGuiFrames Frames;
		DeferredJobs Jobs;
		Core::DocumentSet Documents;
		Graphite::ModalGate Modals;
		SilentServer Server;
		Graphite::DiagnosticsModel Diagnostics;
		NoChangeBases Bases;
		Graphite::ChangeMarkersModel ChangeMarkers{Bases, [](std::function<void()>) {}};
		SilentPreviewServer PreviewServer;
		Graphite::PreviewSemantics PreviewSemantics{PreviewServer};
		Graphite::AnalysisCache Analysis{kAnalysisBudgetBytes};
		Graphite::AnalysisStore AnalysisStore;
		SilentAnalysisServer AnalysisServer;
		NoFileTexts Texts;
		Graphite::WorkspaceAnalysis WorkspaceAnalysis{AnalysisServer, Texts, Analysis, nullptr};
		Core::EventBus Events;
		Graphite::ActiveEditorTarget ActiveEditor;
		Graphite::DocumentWindows Windows{Documents, Modals, Server, Diagnostics, ChangeMarkers, PreviewSemantics,
										  Analysis, AnalysisStore, WorkspaceAnalysis, Events, ActiveEditor};

		TempAppSettings Settings;
		Graphite::Workspace Workspace{Events, Settings.Get()};
		Graphite::ActiveScheme Scheme;
		Graphite::EditorSettings Editor{Settings.Get()};
		Graphite::ReferencesPanel References{Workspace, Documents, Scheme, Editor, PreviewSemantics};
		Core::TabCloseRequests CloseRequests;
		std::filesystem::path NoRoot;
		std::map<std::filesystem::path, ImVec2> WindowSizes;

		DocumentWindowsWithHeldReads()
		{
			Documents.LoadLargeFilesWith(Jobs.Submitter());
		}

		void FinishReads()
		{
			Jobs.RunAll();
			Windows.LandFinishedLoads();
		}

		void DrawFrame(bool wordWrap)
		{
			Frames.Next([this, wordWrap] {
				for (std::size_t index = 0; index < Documents.GetCount(); ++index)
				{
					const std::string windowName = Graphite::DocumentWindowName(Documents, index);

					const std::filesystem::path& path = Documents.At(index).Path;
					const ImVec2 size = WindowSizes.contains(path) ? WindowSizes.at(path) : kDocumentWindowSize;

					if (ImGui::FindWindowByName(windowName.c_str()) != nullptr)
					{
						ImGui::SetWindowSize(windowName.c_str(), size);
					}
				}

				const Graphite::CommandContext noCommands;
				Windows.Render(noCommands,
							   Graphite::EditorFrame{.FontSize = kEditorFontSize,
													 .Indent = Core::IndentStyle{},
													 .FallbackDockNode = 0,
													 .WordWrap = wordWrap,
													 .ChangeMarkers = Graphite::ChangeMarkerDisplay{},
													 .HighlightOccurrences = false,
													 .Hover = Graphite::HoverSettings{},
													 .Scheme = Graphite::DarkScheme(),
													 .CloseRequests = CloseRequests,
													 .WorkspaceRoot = NoRoot},
							   References);
			});
		}
	};

	// WARNING: A finished read reaches the set through its mailbox, which asks for a frame; nothing else in a read does.
	bool RunningThemPostsAResult(DeferredJobs& jobs)
	{
		Core::Redraw::TakeRequest();
		jobs.RunAll();

		return Core::Redraw::IsRequested();
	}

	std::string NumberedLinesPastBackgroundThreshold()
	{
		std::string text;
		std::size_t lineNumber = 0;

		while (text.size() <= Core::BackgroundLoadThreshold)
		{
			text += "line " + std::to_string(lineNumber) + " of a large log\n";
			++lineNumber;
		}

		return text;
	}

	const Graphite::TabState* TabFor(const Graphite::WorkspaceState& state, const std::filesystem::path& path)
	{
		for (const Graphite::TabState& tab : state.Tabs)
		{
			if (tab.Path == path)
			{
				return &tab;
			}
		}

		return nullptr;
	}
}

TEST_CASE("A save failure whose error names the file is shown as it is")
{
	const std::filesystem::path path("D:/Repos/IDE/src/a.cpp");

	CHECK(Graphite::SaveFailureMessage(path, "Cannot write 'D:/Repos/IDE/src/a.cpp': access denied") ==
		  "Cannot write 'D:/Repos/IDE/src/a.cpp': access denied");
}

TEST_CASE("A save failure whose error does not name the file is prefixed with its path")
{
	const std::filesystem::path path("D:/Repos/IDE/src/a.cpp");

	CHECK(Graphite::SaveFailureMessage(path, "cannot write 'D:/Temp/x1.tmp': disk full") ==
		  "D:/Repos/IDE/src/a.cpp: cannot write 'D:/Temp/x1.tmp': disk full");
	CHECK(Graphite::SaveFailureMessage(path, "access denied") == "D:/Repos/IDE/src/a.cpp: access denied");
}

TEST_CASE("A document's window name starts with its file kind's tab glyph, not its badge, and is identified by its path")
{
	TempDirectory folder;
	const std::filesystem::path notes = folder.MakeFile("README.md", "# Notes");

	Core::DocumentSet documents;
	REQUIRE(documents.Open(notes, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	constexpr std::size_t kOnlyDocument = 0;
	const std::string name = Graphite::DocumentWindowName(documents, kOnlyDocument);

	CHECK(name.starts_with(Graphite::GlyphOf(Graphite::FileKind::Markdown)));
	CHECK_FALSE(name.starts_with(Graphite::BadgeOf(Graphite::FileKind::Markdown)));
	CHECK(name.ends_with("###" + Core::ToUtf8(documents.At(kOnlyDocument).Path)));
}

TEST_CASE("A tab whose large file is still being read is remembered as open and active")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RequestOpen(path, true);
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	const Graphite::WorkspaceState state = fixture.Windows.CaptureState();

	const Graphite::TabState* tab = TabFor(state, path);
	REQUIRE(tab != nullptr);
	CHECK_FALSE(tab->Preview);
	CHECK(tab->Line == 0);
	CHECK(tab->Column == 0);
	CHECK(state.Active == path);
}

TEST_CASE("A restored tab whose large file is still being read is remembered where it was")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	Graphite::WorkspaceState restored;
	restored.Tabs.push_back(Graphite::TabState{.Path = path, .Preview = false, .Line = 1200, .Column = 4, .Top = 1180});
	restored.Active = path;

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RestoreState(restored);
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	const Graphite::WorkspaceState state = fixture.Windows.CaptureState();

	const Graphite::TabState* tab = TabFor(state, path);
	REQUIRE(tab != nullptr);
	CHECK(*tab == restored.Tabs.front());
	CHECK(state.Active == path);
}

TEST_CASE("A restored tab whose large file is read on a worker gets its caret and top line once the text lands")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	Graphite::WorkspaceState restored;
	restored.Tabs.push_back(Graphite::TabState{.Path = path, .Preview = false, .Line = 1200, .Column = 4, .Top = 1180});

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RestoreState(restored);
	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	CHECK(item.Caret.Offset == Graphite::RestoredCaretOffset(item.Document->GetBuffer(), 1200, 4));

	const Graphite::WorkspaceState state = fixture.Windows.CaptureState();

	const Graphite::TabState* tab = TabFor(state, path);
	REQUIRE(tab != nullptr);
	CHECK(tab->Line == 1200);
	CHECK(tab->Column == 4);
	CHECK(tab->Top == 1180);
}

TEST_CASE("A tab whose large file comes back reads it on a worker rather than where the change was noticed")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.GetPath() / "appears.log";

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RequestOpen(path, true);
	REQUIRE(fixture.Documents.At(0).Failure == Core::OpenFailure::Missing);

	WriteFileBytes(path, NumberedLinesPastBackgroundThreshold());

	const bool everythingChanged = true;
	fixture.Windows.HandleFilesChanged({}, everythingChanged);

	CHECK(Core::IsLoading(fixture.Documents.At(0)));

	fixture.FinishReads();
	CHECK(fixture.Documents.At(0).Document != nullptr);
}

TEST_CASE("Closing a tab whose file is still being read closes it and gives up the read")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RequestOpen(path, true);
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	const bool wordWrap = false;
	fixture.DrawFrame(wordWrap);

	CHECK_FALSE(fixture.Windows.CanSave());
	REQUIRE(fixture.Windows.CanClose());
	fixture.Windows.Close();
	fixture.DrawFrame(wordWrap);

	CHECK(fixture.Documents.GetCount() == 0);
	CHECK_FALSE(RunningThemPostsAResult(fixture.Jobs));
}

TEST_CASE("A file changed on disk while it was read is reloaded once the read lands")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RequestOpen(path, true);
	fixture.Jobs.RunAll();

	const std::string changedText = NumberedLinesPastBackgroundThreshold() + "one line more\n";
	WriteFileBytes(path, changedText);
	fixture.Windows.LandFinishedLoads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	CHECK(item.Document->GetBuffer().GetLength() == changedText.size());
	CHECK_FALSE(item.Document->CheckChangedOnDisk());
}

TEST_CASE("A tab whose file is still being read saves the selection asked for while it loads")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	const Graphite::OpenDocument openPermanently = [&fixture](const std::filesystem::path& file) { fixture.Windows.RequestOpen(file, true); };

	REQUIRE(Graphite::SelectInDocument(fixture.Documents, path, 3, 2, 5, openPermanently));
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	const Graphite::WorkspaceState state = fixture.Windows.CaptureState();

	const Graphite::TabState* tab = TabFor(state, path);
	REQUIRE(tab != nullptr);
	CHECK(tab->Line == 3);
	CHECK(tab->Column == 5);

	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	const std::size_t lineStart = item.Document->GetBuffer().LineStart(3);
	CHECK(item.Caret.Anchor == lineStart + 2);
	CHECK(item.Caret.Offset == lineStart + 5);
}

TEST_CASE("A UTF-16 caret asked for while a file loads is saved in its unit and restored on the same character")
{
	TempDirectory folder;
	const std::string firstLine = "\xc4\x85\xc4\x85 target";
	const std::filesystem::path path = folder.MakeFile("big.log", firstLine + "\n" + NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	const Graphite::OpenDocument openPermanently = [&fixture](const std::filesystem::path& file) { fixture.Windows.RequestOpen(file, true); };

	constexpr std::size_t kUtf16ColumnOfTarget = 3;
	constexpr std::size_t kByteOfTarget = 5;
	REQUIRE(Graphite::SelectInDocument(fixture.Documents, path, 0, Graphite::Utf16Column(kUtf16ColumnOfTarget), 0,
									   Graphite::Utf16Column(kUtf16ColumnOfTarget), openPermanently));

	const Graphite::WorkspaceState saved = fixture.Windows.CaptureState();
	const Graphite::TabState* tab = TabFor(saved, path);
	REQUIRE(tab != nullptr);
	CHECK(tab->Column == kUtf16ColumnOfTarget);
	CHECK(tab->ColumnIn == Core::ColumnUnit::Utf16CodeUnit);

	fixture.Documents.CloseAll();
	fixture.Windows.RestoreState(saved);
	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	CHECK(item.Caret.Offset == kByteOfTarget);
}

TEST_CASE("A large file read in a narrow pane while a wide pane draws last is wrapped for its own pane")
{
	TempDirectory folder;
	const std::filesystem::path big = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());
	const std::filesystem::path small = folder.MakeFile("small.txt", "a short file\n");

	DocumentWindowsWithHeldReads fixture;
	fixture.WindowSizes[big] = ImVec2(500.0f, 600.0f);
	fixture.WindowSizes[small] = ImVec2(1100.0f, 600.0f);

	const bool wordWrap = true;
	fixture.Windows.RequestOpen(big, true);
	fixture.Windows.RequestOpen(small, true);
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	fixture.DrawFrame(wordWrap);
	fixture.DrawFrame(wordWrap);
	fixture.DrawFrame(wordWrap);
	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	const std::size_t columnsBuiltOnTheWorker = item.ColumnsPerWrappedRow;
	REQUIRE(columnsBuiltOnTheWorker != Core::RowMap::kNoWrap);

	fixture.DrawFrame(wordWrap);
	CHECK(item.ColumnsPerWrappedRow == columnsBuiltOnTheWorker);
}

TEST_CASE("A large file opened with word wrap on is wrapped on the worker as its pane draws it")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	const bool wordWrap = true;

	fixture.Windows.RequestOpen(path, true);
	fixture.DrawFrame(wordWrap);
	fixture.DrawFrame(wordWrap);
	fixture.FinishReads();

	const Core::OpenItem& item = fixture.Documents.At(0);
	REQUIRE(item.Document != nullptr);
	const std::size_t columnsBuiltOnTheWorker = item.ColumnsPerWrappedRow;
	REQUIRE(columnsBuiltOnTheWorker != Core::RowMap::kNoWrap);

	fixture.DrawFrame(wordWrap);
	CHECK(item.ColumnsPerWrappedRow == columnsBuiltOnTheWorker);

	fixture.DrawFrame(wordWrap);
	CHECK(item.ColumnsPerWrappedRow == columnsBuiltOnTheWorker);
}
