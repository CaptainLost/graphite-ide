#include <doctest/doctest.h>

#include "Language/Diagnostics/ProblemsChecking.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

	const std::filesystem::path kFoo{"C:/src/Foo.cpp"};
	const std::filesystem::path kBar{"C:/src/Bar.cpp"};
	const std::filesystem::path kProgram{"C:/src/Program.cs"};
	const std::filesystem::path kNotes{"C:/src/notes.txt"};

	Graphite::ServerStatus Clangd(Graphite::ServerState state)
	{
		return Graphite::ServerStatus{.State = state, .Name = "clangd", .Language = Graphite::LanguageId::Cpp};
	}

	Graphite::ServerStatus RoslynLoading()
	{
		return Graphite::ServerStatus{.State = Graphite::ServerState::Indexing,
									  .Name = "Roslyn",
									  .Loading = "Graphite.csproj",
									  .Language = Graphite::LanguageId::CSharp};
	}

	Graphite::DiagnosticsModel::Entry Sent(int version)
	{
		Graphite::DiagnosticsModel::Entry entry;
		entry.Sent = Graphite::DocumentVersion{version};
		return entry;
	}

	Graphite::DiagnosticsModel::Entry Answered(int sent, int answered)
	{
		Graphite::DiagnosticsModel::Entry entry = Sent(sent);
		entry.Answered = Graphite::DocumentVersion{answered};
		entry.HasAnswer = true;
		return entry;
	}

	Graphite::OpenDocumentCheck Document(const std::filesystem::path& path, Graphite::ServerStatus server,
										 const Graphite::DiagnosticsModel::Entry* entry)
	{
		const Graphite::LanguageId language = server.Language;
		return Graphite::OpenDocumentCheck{.Path = path, .Language = language, .Server = std::move(server), .Entry = entry};
	}
}

TEST_CASE("A document answered for the version it was last sent is checked whatever its server does now")
{
	const Graphite::DiagnosticsModel::Entry answered = Answered(3, 3);

	CHECK(Graphite::CheckOf(Document(kFoo, Clangd(Graphite::ServerState::Ready), &answered)) == Graphite::DocumentCheck::Checked);
	CHECK(Graphite::CheckOf(Document(kFoo, Clangd(Graphite::ServerState::Failed), &answered)) == Graphite::DocumentCheck::Checked);
}

TEST_CASE("A document edited since its answer, or never answered, is still being checked by a server that answers")
{
	const Graphite::DiagnosticsModel::Entry stale = Answered(4, 3);
	const Graphite::DiagnosticsModel::Entry unanswered = Sent(1);

	CHECK(Graphite::CheckOf(Document(kFoo, Clangd(Graphite::ServerState::Ready), &stale)) == Graphite::DocumentCheck::Checking);
	CHECK(Graphite::CheckOf(Document(kFoo, Clangd(Graphite::ServerState::Ready), &unanswered)) ==
		  Graphite::DocumentCheck::Checking);
}

TEST_CASE("A document waits for a server that starts or loads its projects, and is not checked by one that cannot run")
{
	const Graphite::DiagnosticsModel::Entry unanswered = Sent(1);

	CHECK(Graphite::CheckOf(Document(kProgram, RoslynLoading(), &unanswered)) == Graphite::DocumentCheck::WaitingForServer);
	CHECK(Graphite::CheckOf(Document(kFoo, Clangd(Graphite::ServerState::Starting), nullptr)) ==
		  Graphite::DocumentCheck::WaitingForServer);
	CHECK(Graphite::CheckOf(Document(kFoo, Clangd(Graphite::ServerState::NotInstalled), nullptr)) ==
		  Graphite::DocumentCheck::ServerUnavailable);
}

TEST_CASE("A document the server gave up answering for its latest version reads as unanswered")
{
	Graphite::DiagnosticsModel::Entry givenUp = Sent(2);
	givenUp.GaveUpOn = Graphite::DocumentVersion{2};

	CHECK(Graphite::CheckOf(Document(kFoo, Clangd(Graphite::ServerState::Ready), &givenUp)) ==
		  Graphite::DocumentCheck::ServerGaveNoAnswer);

	givenUp.Sent = Graphite::DocumentVersion{3};

	CHECK(Graphite::CheckOf(Document(kFoo, Clangd(Graphite::ServerState::Ready), &givenUp)) == Graphite::DocumentCheck::Checking);
}

TEST_CASE("A document never sent to a running server is not being checked, and plain text is left out")
{
	CHECK(Graphite::CheckOf(Document(kFoo, Clangd(Graphite::ServerState::Ready), nullptr)) == Graphite::DocumentCheck::NeverSent);

	const Graphite::OpenDocumentCheck notes{.Path = kNotes, .Language = Graphite::LanguageId::PlainText};

	CHECK(Graphite::CheckOf(notes) == Graphite::DocumentCheck::NotSupported);
}

TEST_CASE("Every document answered leaves nothing still to check")
{
	const Graphite::DiagnosticsModel::Entry answered = Answered(1, 1);

	const Graphite::ProblemsChecking checking = Graphite::SummariseChecking(
		{Document(kFoo, Clangd(Graphite::ServerState::Ready), &answered),
		 Graphite::OpenDocumentCheck{.Path = kNotes, .Language = Graphite::LanguageId::PlainText}});

	CHECK(checking.EverythingChecked());
	CHECK(checking.AnyAnswered);
}

TEST_CASE("Nothing answered while Roslyn loads its projects waits for Roslyn by name and counts the files")
{
	const Graphite::DiagnosticsModel::Entry unanswered = Sent(1);

	const Graphite::ProblemsChecking checking = Graphite::SummariseChecking(
		{Document(kProgram, RoslynLoading(), &unanswered), Document("C:/src/Other.cs", RoslynLoading(), &unanswered),
		 Document("C:/src/Third.cs", RoslynLoading(), nullptr)});

	CHECK_FALSE(checking.EverythingChecked());
	CHECK_FALSE(checking.AnyAnswered);
	CHECK(Graphite::NothingCheckedYetText(checking) ==
		  "Waiting for Roslyn to load Graphite.csproj \xC2\xB7 3 open files not checked yet");
}

TEST_CASE("Files still being checked are named beside the problems already found")
{
	const Graphite::DiagnosticsModel::Entry answered = Answered(1, 1);
	const Graphite::DiagnosticsModel::Entry unanswered = Sent(1);

	const Graphite::ProblemsChecking checking = Graphite::SummariseChecking(
		{Document("C:/src/Done.cpp", Clangd(Graphite::ServerState::Ready), &answered),
		 Document(kFoo, Clangd(Graphite::ServerState::Ready), &unanswered),
		 Document(kBar, Clangd(Graphite::ServerState::Ready), &unanswered)});

	CHECK(checking.AnyAnswered);
	CHECK(Graphite::StillCheckingText(checking) == "Still checking Foo.cpp, Bar.cpp");
}

TEST_CASE("An edited file with no problems so far says it is being checked again")
{
	const Graphite::DiagnosticsModel::Entry stale = Answered(2, 1);

	const Graphite::ProblemsChecking checking =
		Graphite::SummariseChecking({Document(kFoo, Clangd(Graphite::ServerState::Ready), &stale)});

	CHECK(checking.AnyAnswered);
	CHECK(Graphite::NoProblemsSoFarText(checking) == "No problems so far \xC2\xB7 checking Foo.cpp\xE2\x80\xA6");
}

TEST_CASE("A language whose server cannot run gets one line however many of its files are open")
{
	const Graphite::ProblemsChecking checking =
		Graphite::SummariseChecking({Document(kFoo, Clangd(Graphite::ServerState::NotInstalled), nullptr),
									 Document(kBar, Clangd(Graphite::ServerState::NotInstalled), nullptr)});

	REQUIRE(checking.UncheckedLanguages.size() == 1);
	CHECK(checking.UncheckedLanguages[0] == "C++ files are not checked: clangd not installed");
	CHECK_FALSE(checking.EverythingChecked());
	CHECK_FALSE(checking.AnyStillComing());
}

TEST_CASE("A file the server never answered and files never shown are said apart")
{
	Graphite::DiagnosticsModel::Entry givenUp = Sent(1);
	givenUp.GaveUpOn = Graphite::DocumentVersion{1};

	const Graphite::ProblemsChecking checking =
		Graphite::SummariseChecking({Document(kFoo, Clangd(Graphite::ServerState::Ready), &givenUp),
									 Document(kBar, Clangd(Graphite::ServerState::Ready), nullptr)});

	REQUIRE(checking.NotAnswered.size() == 1);
	CHECK(Graphite::NotAnsweredText(checking.NotAnswered[0]) == "Foo.cpp was not checked: clangd did not answer");
	CHECK(Graphite::NeverSentText(checking) == "Not checked until shown: Bar.cpp");
	CHECK_FALSE(checking.AnyStillComing());
}

TEST_CASE("A long list of files names three and counts the rest")
{
	const std::vector<std::filesystem::path> paths{"C:/a.cpp", "C:/b.cpp", "C:/c.cpp", "C:/d.cpp", "C:/e.cpp"};

	CHECK(Graphite::FileNamesText(paths) == "a.cpp, b.cpp, c.cpp and 2 more");
}
