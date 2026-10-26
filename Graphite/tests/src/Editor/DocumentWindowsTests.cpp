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
#include "Persistence/CaretRestore.h"
#include "Persistence/PersistedState.h"
#include "UI/Dialogs/ModalGate.h"

#include "DeferredJobs.h"
#include "FileFixtures.h"
#include "TempDirectory.h"

#include "Core/Events/EventBus.h"
#include "Core/FileSystem/FileSystem.h"
#include "Core/Text/DocumentSet.h"
#include "Core/Text/FileProbe.h"

#include <cstddef>
#include <filesystem>
#include <functional>
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

	struct DocumentWindowsWithHeldReads
	{
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

		DocumentWindowsWithHeldReads()
		{
			Documents.LoadLargeFilesWith(Jobs.Submitter());
		}

		void FinishReads()
		{
			Jobs.RunAll();
			Windows.LandFinishedLoads();
		}
	};

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

TEST_CASE("Closing works on a tab whose file is still being read")
{
	TempDirectory folder;
	const std::filesystem::path path = folder.MakeFile("big.log", NumberedLinesPastBackgroundThreshold());

	DocumentWindowsWithHeldReads fixture;
	fixture.Windows.RequestOpen(path, true);
	REQUIRE(Core::IsLoading(fixture.Documents.At(0)));

	CHECK(fixture.Windows.CanClose());
	CHECK_FALSE(fixture.Windows.CanSave());
}
