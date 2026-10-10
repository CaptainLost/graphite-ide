#include <doctest/doctest.h>

#include "InstalledServers.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/ServerChoice.h"
#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"

#include <chrono>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

	std::size_t g_AsksOfFirstProvider = 0;
	std::size_t g_AsksOfSecondProvider = 0;

	Graphite::ServerChoice AnswerAsFirstProvider(Graphite::LanguageId language)
	{
		++g_AsksOfFirstProvider;
		return Graphite::ChooseServer(language, std::string{Graphite::kServerTurnedOff});
	}

	Graphite::ServerChoice AnswerAsSecondProvider(Graphite::LanguageId language)
	{
		++g_AsksOfSecondProvider;
		return Graphite::ChooseServer(language, std::string{Graphite::kServerTurnedOff});
	}

	Graphite::ServerChoice ClangdAtFor(Graphite::LanguageId language, const std::filesystem::path& binary)
	{
		if (language != Graphite::LanguageId::Cpp)
		{
			return Graphite::ChooseServer(language, std::nullopt);
		}

		return Graphite::ServerChoice{.Kind = Graphite::ServerChoiceKind::Chosen,
									  .Server = Graphite::FindServer("clangd"),
									  .Overrides = Graphite::ServerOverrides{.Binary = binary}};
	}

	void UseClangdAt(Graphite::LanguageServices& services, const std::filesystem::path& binary)
	{
		services.SetChoiceProvider([binary](Graphite::LanguageId language) { return ClangdAtFor(language, binary); });
	}
}

TEST_CASE("A language with no configured server reports NotConfigured")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	services.EnsureStarted(temp.GetPath() / "notes.txt", Graphite::LanguageId::PlainText);

	CHECK(services.StatusFor(Graphite::LanguageId::PlainText).State ==
		  Graphite::ServerState::NotConfigured);
}

TEST_CASE("A configured server with no binary reports NotFound and names what is missing")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services,
								 temp.GetPath() / "no-such-clangd.exe");
	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	const Graphite::ServerStatus status = services.StatusFor(Graphite::LanguageId::Cpp);

	CHECK(status.State == Graphite::ServerState::NotFound);
	CHECK(status.Name == "clangd");
	CHECK_FALSE(status.Detail.empty());
}

// The shape the editor is in on a machine with no clangd, which is every machine until one is
// installed: the language IS configured, so a server entry exists and reports NotFound, but it
// never got a client. That is a different path from a language with no entry at all, and it is
// the only path such a machine ever takes.
TEST_CASE("A ticket asked of a configured server whose binary is missing comes back Failed")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services,
								 temp.GetPath() / "no-such-clangd.exe");

	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);

	REQUIRE(services.StatusFor(Graphite::LanguageId::Cpp).State ==
			Graphite::ServerState::NotFound);

	Graphite::FilePosition where;
	where.Path = source;
	where.Line = 0;
	where.Column = Graphite::Utf16Column(0);

	const Graphite::LanguageServices::Ticket ticket =
		services.AskDefinition(source, Graphite::LanguageId::Cpp, where);

	services.Poll();

	std::vector<Graphite::LanguageServices::Answer> answers;
	services.TakeAnswers(answers);

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].For == ticket);
	CHECK(answers[0].Failed);
}

// The point of warming up: a server for a language the tree contains starts from the folder
// alone, with no document of that language ever opened. Without it the first file opened is also
// the first thing indexed, so the counts arrive late on exactly the file being read.
TEST_CASE("Opening a folder starts a server for a language the tree contains")
{
	TempDirectory temp;

	temp.MakeFile("notes.txt", "nothing to see\n");
	temp.MakeFile("src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{temp.GetPath()};

	// A binary that does not exist, so this proves a server was STARTED without depending on a
	// clangd being installed: NotFound is a state only reachable by having tried.
	UseClangdAt(services, temp.GetPath() / "absent.exe");

	services.WarmUp(temp.GetPath(), false);

	// The walk runs on a worker and Poll is what turns what it found into servers, so this polls
	// rather than assuming. Bounded, so a warm-up that never reports fails the test instead of
	// hanging the suite - the same reason JobQueueTests bounds its own loop.
	constexpr int kMaxAttempts = 500;
	int attempt = 0;

	for (; attempt < kMaxAttempts; ++attempt)
	{
		services.Poll();

		if (services.StatusFor(Graphite::LanguageId::Cpp).State !=
			Graphite::ServerState::NotConfigured)
		{
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(attempt < kMaxAttempts);
	CHECK(services.StatusFor(Graphite::LanguageId::Cpp).State == Graphite::ServerState::NotFound);
}

// What keeps the status bar from going silent at exactly the wrong moment: a server started by
// opening the folder has to be reportable before any document of its language is open, because
// on a fresh start there is no such document and its indexing is the only thing happening.
TEST_CASE("A server started by opening a folder is reportable with no document of its language")
{
	TempDirectory temp;

	temp.MakeFile("src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{temp.GetPath()};
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.WarmUp(temp.GetPath(), false);

	constexpr int kMaxAttempts = 500;
	int attempt = 0;

	for (; attempt < kMaxAttempts; ++attempt)
	{
		services.Poll();

		if (services.OverallStatus().has_value())
		{
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	REQUIRE(attempt < kMaxAttempts);

	const std::optional<Graphite::ServerStatus> overall = services.OverallStatus();

	REQUIRE(overall.has_value());
	CHECK(overall->State == Graphite::ServerState::NotFound);
	CHECK(overall->Name == "clangd");
}

TEST_CASE("A workspace with no server started has nothing to report")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	// Nothing started at all, so nothing to report - the resting case the bar stays quiet for.
	CHECK_FALSE(services.OverallStatus().has_value());
}

TEST_CASE("A folder holding no source of a configured language starts nothing")
{
	TempDirectory temp;

	temp.MakeFile("notes.txt", "nothing to see\n");
	temp.MakeFile("data/readme.md", "# nothing\n");

	Graphite::LanguageServices services{temp.GetPath()};
	UseClangdAt(services, temp.GetPath() / "absent.exe");

	services.WarmUp(temp.GetPath(), false);

	// Long enough for the walk to have finished this two-file tree several times over.
	for (int attempt = 0; attempt < 30; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(services.StatusFor(Graphite::LanguageId::Cpp).State ==
		  Graphite::ServerState::NotConfigured);
}

TEST_CASE("EnsureStarted is idempotent for the same root")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.EnsureStarted(temp.GetPath() / "a.cpp", Graphite::LanguageId::Cpp);
	services.EnsureStarted(temp.GetPath() / "b.cpp", Graphite::LanguageId::Cpp);

	CHECK(services.StatusFor(Graphite::LanguageId::Cpp).Restarts == 0);
}

TEST_CASE("Restart backoff grows with the number of restarts already attempted")
{
	// Pure policy, checked without a process: the delay before restart attempt N is N*2 seconds,
	// so the first attempt after a death is immediate and each further one waits longer. Poll
	// waiting it out is tested through a client factory further down.
	CHECK(Graphite::RestartBackoff(0) == std::chrono::seconds(0));
	CHECK(Graphite::RestartBackoff(1) == std::chrono::seconds(2));
	CHECK(Graphite::RestartBackoff(2) == std::chrono::seconds(4));
}

TEST_CASE("A server may restart until it has used up kMaximumRestarts")
{
	// The budget rule on its own, as the pure function Poll consults. The restart path that obeys it
	// is driven through a client factory further down.
	CHECK(Graphite::MayRestart(0));
	CHECK(Graphite::MayRestart(2));
	CHECK_FALSE(Graphite::MayRestart(3));
	CHECK_FALSE(Graphite::MayRestart(4));
}

TEST_CASE("The restart budget refills only after the server has stayed up for kStableAfter")
{
	using Point = std::chrono::steady_clock::time_point;
	const Point since{std::chrono::seconds(1000)};

	// Down (no start recorded): never refilled.
	CHECK_FALSE(Graphite::RestartBudgetRefilled(std::nullopt, since + std::chrono::hours(1)));

	// Up, but not for long enough yet.
	CHECK_FALSE(Graphite::RestartBudgetRefilled(since, since));
	CHECK_FALSE(Graphite::RestartBudgetRefilled(since, since + Graphite::kStableAfter - std::chrono::seconds(1)));

	// Stable for the full period.
	CHECK(Graphite::RestartBudgetRefilled(since, since + Graphite::kStableAfter));
	CHECK(Graphite::RestartBudgetRefilled(since, since + std::chrono::hours(1)));
}

TEST_CASE("Tickets are unique across two different servers")
{
	TempDirectory tempA;
	TempDirectory tempB;

	Graphite::LanguageServices services{tempA.GetPath()};
	UseClangdAt(services, tempA.GetPath() / "absent.exe");

	// Neither root ever gets a live client (the binary does not exist), so both tickets are
	// failed on the spot — but a ticket is issued before that is known, and issuing is what
	// this case checks: two different (language, root) keys must never hand out the same one.
	const Graphite::LanguageServices::Ticket first =
		services.AskDefinition(tempA.GetPath() / "a.cpp", Graphite::LanguageId::Cpp, {});
	const Graphite::LanguageServices::Ticket second =
		services.AskDefinition(tempB.GetPath() / "b.cpp", Graphite::LanguageId::Cpp, {});

	CHECK(first != second);
}

TEST_CASE("A ticket for a language with no configured server is answered Failed immediately")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	const Graphite::LanguageServices::Ticket ticket = services.AskSymbols(
		temp.GetPath() / "notes.txt", Graphite::LanguageId::PlainText);

	std::vector<Graphite::LanguageServices::Answer> answers;
	services.TakeAnswers(answers);

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].For == ticket);
	CHECK(answers[0].Failed);
}

TEST_CASE("A semantic-tokens ticket the client refuses before the handshake is failed at once")
{
	// where.exe never completes a handshake, so the client has no semantic legend and declines to
	// ask. Its "not asked" request id must not be parked as a pending request: nothing would ever
	// answer it, and the editor would wait on the ticket for the lifetime of the tab. Taken without
	// a Poll, so an answer that only arrives when the process dies does not count.
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, "C:\\Windows\\System32\\where.exe");
	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	const Graphite::LanguageServices::Ticket ticket =
		services.AskSemanticTokens(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	std::vector<Graphite::LanguageServices::Answer> answers;
	services.TakeAnswers(answers);

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].For == ticket);
	CHECK(answers[0].Failed);
}

TEST_CASE("TakeAnswers empties its queue")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	services.AskSymbols(temp.GetPath() / "notes.txt", Graphite::LanguageId::PlainText);

	std::vector<Graphite::LanguageServices::Answer> first;
	services.TakeAnswers(first);
	CHECK(first.size() == 1);

	std::vector<Graphite::LanguageServices::Answer> second;
	services.TakeAnswers(second);
	CHECK(second.empty());
}

TEST_CASE("A ticket outstanding when its server dies is answered Failed, never left hanging")
{
	// where.exe is a real executable, not a fake path: LspClient always launches its binary with
	// "--background-index", where.exe treats that as a pattern it cannot find anything for,
	// prints its "could not find" message, and exits within a few hundred milliseconds — a
	// process that starts and then genuinely dies mid-conversation, without depending on clangd
	// being installed here.
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, "C:\\Windows\\System32\\where.exe");
	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	const Graphite::LanguageServices::Ticket ticket =
		services.AskSymbols(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	// Bounded so a broken implementation fails this test rather than hanging the suite — the
	// same reasoning JobQueueTests' cancellation test uses for its own polling loop.
	constexpr int kMaxAttempts = 500;
	bool delivered = false;
	bool failed = false;
	int attempt = 0;

	for (; attempt < kMaxAttempts && !delivered; ++attempt)
	{
		services.Poll();

		std::vector<Graphite::LanguageServices::Answer> answers;
		services.TakeAnswers(answers);

		for (const Graphite::LanguageServices::Answer& answer : answers)
		{
			if (answer.For == ticket)
			{
				delivered = true;
				failed = answer.Failed;
			}
		}

		if (!delivered)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}

	// attempt < kMaxAttempts proves the loop stopped because the ticket arrived, not because it
	// ran out of patience — the same distinction JobQueueTests draws with `rounds`.
	CHECK(attempt < kMaxAttempts);
	CHECK(delivered);
	CHECK(failed);
}

TEST_CASE("A fresh services object has nothing to say about diagnostics")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	Graphite::DiagnosticsUpdate update;
	services.TakeDiagnostics(update);

	CHECK(update.Published.empty());
	CHECK(update.Versions.empty());
	CHECK(update.Closed.empty());
}

TEST_CASE("A document a server never received produces no note")
{
	// The shape of every machine with no clangd installed: the language is configured, so an
	// entry exists and reports NotFound, but nothing was ever sent. A version note here would
	// claim a document was sent under a version that never went anywhere, and the model would
	// then hold an entry for a file no server has ever heard of.
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services,
								 temp.GetPath() / "no-such-clangd.exe");

	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "int main() {}\n");
	services.CloseDocument(source, Graphite::LanguageId::Cpp);

	Graphite::DiagnosticsUpdate update;
	services.TakeDiagnostics(update);

	CHECK(update.Versions.empty());
	CHECK(update.Closed.empty());
}

// Every case below configures a binary that does not exist, as the NotFound cases above do:
// NotFound is only reachable by having tried to start, so it tells "started" from "refused"
// without a clangd installed.

TEST_CASE("A root the permission refuses gets no server, only a restricted entry")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([](const std::filesystem::path&) { return false; });

	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	const Graphite::ServerStatus status = services.StatusFor(Graphite::LanguageId::Cpp);

	CHECK(status.State == Graphite::ServerState::Restricted);
	CHECK(status.Name == "clangd");
}

TEST_CASE("Revalidate starts a restricted server once its root is permitted")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	bool permitted = false;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&permitted](const std::filesystem::path&) { return permitted; });

	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);
	REQUIRE(services.StatusFor(Graphite::LanguageId::Cpp).State == Graphite::ServerState::Restricted);

	permitted = true;
	services.Revalidate();

	CHECK(services.StatusFor(Graphite::LanguageId::Cpp).State == Graphite::ServerState::NotFound);
}

TEST_CASE("Revalidate restricts a server whose root is no longer permitted")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	bool permitted = true;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&permitted](const std::filesystem::path&) { return permitted; });

	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);
	REQUIRE(services.StatusFor(Graphite::LanguageId::Cpp).State == Graphite::ServerState::NotFound);

	permitted = false;
	services.Revalidate();

	CHECK(services.StatusFor(Graphite::LanguageId::Cpp).State == Graphite::ServerState::Restricted);
}

TEST_CASE("A ticket asked of a restricted server comes back Failed")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([](const std::filesystem::path&) { return false; });

	const std::filesystem::path source = temp.GetPath() / "main.cpp";
	services.EnsureStarted(source, Graphite::LanguageId::Cpp);

	Graphite::FilePosition where;
	where.Path = source;
	where.Line = 0;
	where.Column = Graphite::Utf16Column(0);

	const Graphite::LanguageServices::Ticket ticket =
		services.AskDefinition(source, Graphite::LanguageId::Cpp, where);

	services.Poll();

	std::vector<Graphite::LanguageServices::Answer> answers;
	services.TakeAnswers(answers);

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].For == ticket);
	CHECK(answers[0].Failed);
}

TEST_CASE("Warming up a refused root starts nothing")
{
	TempDirectory temp;
	temp.MakeFile("src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([](const std::filesystem::path&) { return false; });

	services.WarmUp(temp.GetPath(), false);

	// Long enough for the permitted case above to have found main.cpp many times over. A
	// negative can only be bounded, never proven, so this gives the walk every chance.
	for (int attempt = 0; attempt < 50; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(services.StatusFor(Graphite::LanguageId::Cpp).State == Graphite::ServerState::NotConfigured);
}

// A tab open on the very file the warm-up walk happens to find - a tab restored at startup, or
// one kept on a restricted server that has just been trusted. The warm-up's own open and close
// must not touch it: a close there would make the server forget the tab, and every edit after
// would be dropped as a change to a document it was never told about.
TEST_CASE("Warming up leaves a document the server already holds open")
{
	TempDirectory temp;
	temp.MakeFile("src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, temp.GetPath() / "absent.exe");

	const std::filesystem::path source = temp.GetPath() / "src" / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "edited in the tab\n");
	REQUIRE(services.Holds(source, Graphite::LanguageId::Cpp));

	services.WarmUp(temp.GetPath(), false);

	// Long enough for the walk to have found main.cpp many times over, as the permitted warm-up
	// test above shows; what it found is only acted on inside Poll.
	for (int attempt = 0; attempt < 50; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(services.Holds(source, Graphite::LanguageId::Cpp));
}

TEST_CASE("A restricted server is reported for the workspace when nothing else is")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([](const std::filesystem::path&) { return false; });

	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	const std::optional<Graphite::ServerStatus> overall = services.OverallStatus();

	REQUIRE(overall.has_value());
	CHECK(overall->State == Graphite::ServerState::Restricted);
}

// A trusted workspace with one file opened from an untrusted folder elsewhere. The untrusted
// folder's restricted entry is about that folder alone; a tab in the trusted one has to be told
// about its own server, whatever order the two roots sort in.
TEST_CASE("A document's status is its own root's, not another root's of the same language")
{
	TempDirectory temp;

	// compile_flags.txt is where RootFor stops, so each folder is a root of its own. The refused
	// one sorts first, so a lookup by language alone would answer with it.
	temp.MakeFile("other/compile_flags.txt", "");
	temp.MakeFile("work/compile_flags.txt", "");

	const std::filesystem::path refused = temp.GetPath() / "other" / "main.cpp";
	const std::filesystem::path permitted = temp.GetPath() / "work" / "main.cpp";
	const std::filesystem::path refusedRoot = temp.GetPath() / "other";

	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&refusedRoot](const std::filesystem::path& root) { return root != refusedRoot; });

	services.EnsureStarted(refused, Graphite::LanguageId::Cpp);
	services.EnsureStarted(permitted, Graphite::LanguageId::Cpp);

	CHECK(services.StatusFor(permitted, Graphite::LanguageId::Cpp).State ==
		  Graphite::ServerState::NotFound);
	CHECK(services.StatusFor(refused, Graphite::LanguageId::Cpp).State ==
		  Graphite::ServerState::Restricted);
}

TEST_CASE("A document with no server entry of its own reports NotConfigured")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	CHECK(services.StatusFor(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp).State ==
		  Graphite::ServerState::NotConfigured);
}

// A trusted server is the more useful thing for the workspace slot to show than an untrusted one
// somewhere else, which already speaks for itself in its own tabs. Ready cannot be reached
// without a live clangd, so this pins the ordering through NotFound, which ranks with it above
// Restricted; the refused root sorts first, so a tie would still answer Restricted.
TEST_CASE("A trusted root's server outranks a restricted one for the workspace")
{
	TempDirectory temp;

	temp.MakeFile("other/compile_flags.txt", "");
	temp.MakeFile("work/compile_flags.txt", "");

	const std::filesystem::path refusedRoot = temp.GetPath() / "other";

	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&refusedRoot](const std::filesystem::path& root) { return root != refusedRoot; });

	services.EnsureStarted(temp.GetPath() / "other" / "main.cpp", Graphite::LanguageId::Cpp);
	services.EnsureStarted(temp.GetPath() / "work" / "main.cpp", Graphite::LanguageId::Cpp);

	const std::optional<Graphite::ServerStatus> overall = services.OverallStatus();

	REQUIRE(overall.has_value());
	CHECK(overall->State == Graphite::ServerState::NotFound);
}

// A file with no marker above it used to resolve to one shared empty root, whatever folder it
// came from, so one trust answer covered files from folders the user had judged differently.
// Each such file now gets a root of its own to be judged by: the open folder when it lies inside
// it, its own directory otherwise. The roots are observed through the permission callback, which
// is asked about exactly the root a server would be started for.
TEST_CASE("A file with no marker inside the fallback root resolves to the fallback root")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	std::vector<std::filesystem::path> asked;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&asked](const std::filesystem::path& root) {
		asked.push_back(root);
		return false;
	});

	services.EnsureStarted(temp.GetPath() / "workspace" / "src" / "main.cpp", Graphite::LanguageId::Cpp);

	REQUIRE(asked.size() == 1);
	CHECK(asked.front() == temp.GetPath() / "workspace");
}

TEST_CASE("A file with no marker outside the fallback root resolves to its own directory")
{
	TempDirectory temp;
	temp.MakeFile("workspace/readme.txt", "\n");
	temp.MakeFile("elsewhere/loose.cpp", "int x;\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	std::vector<std::filesystem::path> asked;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&asked](const std::filesystem::path& root) {
		asked.push_back(root);
		return false;
	});

	services.EnsureStarted(temp.GetPath() / "elsewhere" / "loose.cpp", Graphite::LanguageId::Cpp);

	REQUIRE(asked.size() == 1);
	CHECK(asked.front() == temp.GetPath() / "elsewhere");
}

// No folder open is an empty fallback root, and the file still gets a root it can be judged by
// rather than an empty one no trust entry can ever cover.
TEST_CASE("A file with no marker and no fallback root resolves to its own directory")
{
	TempDirectory temp;
	temp.MakeFile("loose/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{{}};
	std::vector<std::filesystem::path> asked;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&asked](const std::filesystem::path& root) {
		asked.push_back(root);
		return false;
	});

	services.EnsureStarted(temp.GetPath() / "loose" / "main.cpp", Graphite::LanguageId::Cpp);

	REQUIRE(asked.size() == 1);
	CHECK(asked.front() == temp.GetPath() / "loose");
}

// The fallback follows the open folder, and a directory resolved against the previous one must
// not keep that answer.
TEST_CASE("Changing the fallback root re-resolves a directory already seen")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{{}};
	std::vector<std::filesystem::path> asked;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&asked](const std::filesystem::path& root) {
		asked.push_back(root);
		return false;
	});

	const std::filesystem::path source = temp.GetPath() / "workspace" / "src" / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.SetFallbackRoot(temp.GetPath() / "workspace");
	services.EnsureStarted(source, Graphite::LanguageId::Cpp);

	REQUIRE(asked.size() == 2);
	CHECK(asked[0] == temp.GetPath() / "workspace" / "src");
	CHECK(asked[1] == temp.GetPath() / "workspace");
}

namespace {
	// Every root the permission is asked about, in order: a new server entry is created exactly
	// when its root is asked about, so this is how a test sees which server a call went to.
	struct AskedRoots
	{
		std::vector<std::filesystem::path> Roots;

		Graphite::LanguageServices::RootPermission Refusing()
		{
			return [this](const std::filesystem::path& root) {
				Roots.push_back(root);
				return false;
			};
		}
	};
}

// The orphaning 76d329b introduced: re-resolving an open document after the fallback moved sent
// its edits and its close to a server that never received didOpen.
TEST_CASE("An open document keeps its server when the fallback root moves")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{{}};
	UseClangdAt(services, temp.GetPath() / "absent.exe");

	const std::filesystem::path source = temp.GetPath() / "workspace" / "src" / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "int main() { return 0; }\n");
	REQUIRE(services.Holds(source, Graphite::LanguageId::Cpp));

	services.SetFallbackRoot(temp.GetPath() / "workspace");

	CHECK(services.Holds(source, Graphite::LanguageId::Cpp));

	services.ChangeDocument(source, Graphite::LanguageId::Cpp, "int main() { return 1; }\n");
	CHECK(services.Holds(source, Graphite::LanguageId::Cpp));

	services.CloseDocument(source, Graphite::LanguageId::Cpp);
	CHECK_FALSE(services.Holds(source, Graphite::LanguageId::Cpp));
}

TEST_CASE("An open document's status is its own server's after the fallback root moves")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{{}};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Refusing());

	const std::filesystem::path source = temp.GetPath() / "workspace" / "src" / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "x\n");
	services.SetFallbackRoot(temp.GetPath() / "workspace");

	// Per-frame EnsureStarted for the active tab must not start a second server for it.
	services.EnsureStarted(source, Graphite::LanguageId::Cpp);

	CHECK(asked.Roots.size() == 1);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).State == Graphite::ServerState::Restricted);
}

TEST_CASE("A markerless file inside the open folder resolves to it whatever the case of its path")
{
	TempDirectory temp;
	temp.MakeFile("Workspace/Src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{temp.GetPath() / "Workspace"};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Refusing());

	// The same file spelled in another case, as a path from a URI or the command line can be.
	std::wstring lowered = (temp.GetPath() / "Workspace" / "Src" / "main.cpp").native();
	for (wchar_t& character : lowered)
	{
		character = static_cast<wchar_t>(towlower(character));
	}

	services.EnsureStarted(std::filesystem::path{lowered}, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 1);
	CHECK(asked.Roots.front() == temp.GetPath() / "Workspace");
}

// F12 into a header outside the folder, with no marker of its own - the system and SDK headers.
TEST_CASE("A markerless file a server named resolves to that server's root")
{
	TempDirectory temp;
	temp.MakeFile("project/compile_flags.txt", "-std=c++20\n");
	temp.MakeFile("project/main.cpp", "#include <vector>\n");
	temp.MakeFile("sdk/include/vector", "// header\n");

	Graphite::LanguageServices services{temp.GetPath() / "project"};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Refusing());

	const std::filesystem::path source = temp.GetPath() / "project" / "main.cpp";
	const std::filesystem::path header = temp.GetPath() / "sdk" / "include" / "vector";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.NoteNamed(header, source, Graphite::LanguageId::Cpp);
	services.EnsureStarted(header, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 1);
	CHECK(asked.Roots.front() == temp.GetPath() / "project");
}

// A header that has a project of its own is that project's, however it was reached.
TEST_CASE("A named file with a marker of its own keeps its own root")
{
	TempDirectory temp;
	temp.MakeFile("project/compile_flags.txt", "-std=c++20\n");
	temp.MakeFile("project/main.cpp", "\n");
	temp.MakeFile("library/compile_flags.txt", "-std=c++20\n");
	temp.MakeFile("library/lib.h", "\n");

	Graphite::LanguageServices services{temp.GetPath() / "project"};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Refusing());

	const std::filesystem::path source = temp.GetPath() / "project" / "main.cpp";
	const std::filesystem::path header = temp.GetPath() / "library" / "lib.h";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.NoteNamed(header, source, Graphite::LanguageId::Cpp);
	services.EnsureStarted(header, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 2);
	CHECK(asked.Roots[1] == temp.GetPath() / "library");
}

// The open folder comes before the server that named a file: a markerless file inside the folder
// is the folder's, even when a server rooted elsewhere pointed at it.
TEST_CASE("A markerless file inside the open folder stays the folder's when another server names it")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/shared.h", "\n");
	temp.MakeFile("library/compile_flags.txt", "-std=c++20\n");
	temp.MakeFile("library/lib.cpp", "\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Refusing());

	const std::filesystem::path source = temp.GetPath() / "library" / "lib.cpp";
	const std::filesystem::path header = temp.GetPath() / "workspace" / "src" / "shared.h";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.NoteNamed(header, source, Graphite::LanguageId::Cpp);
	services.EnsureStarted(header, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 2);
	CHECK(asked.Roots[1] == temp.GetPath() / "workspace");
}

namespace {
	struct FakeClock
	{
		std::chrono::steady_clock::time_point Now = std::chrono::steady_clock::time_point{} + std::chrono::hours(1);

		Graphite::LanguageServices::Clock Get()
		{
			return [this] { return Now; };
		}
	};
}

TEST_CASE("An idle server outside the open folder is shut down after the idle delay, not before")
{
	TempDirectory temp;
	temp.MakeFile("workspace/readme.txt", "\n");
	temp.MakeFile("elsewhere/compile_flags.txt", "\n");
	temp.MakeFile("elsewhere/loose.cpp", "int x;\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	FakeClock clock;
	services.SetClock(clock.Get());
	UseClangdAt(services, temp.GetPath() / "absent.exe");

	const std::filesystem::path loose = temp.GetPath() / "elsewhere" / "loose.cpp";

	services.EnsureStarted(loose, Graphite::LanguageId::Cpp);
	services.Poll();

	clock.Now += Graphite::kIdleShutdown - std::chrono::seconds(1);
	services.Poll();
	CHECK(services.IsRunningFor(loose, Graphite::LanguageId::Cpp));

	clock.Now += std::chrono::seconds(2);
	services.Poll();
	CHECK_FALSE(services.IsRunningFor(loose, Graphite::LanguageId::Cpp));
}

TEST_CASE("The open folder's server is never shut down for being idle")
{
	TempDirectory temp;
	temp.MakeFile("workspace/compile_flags.txt", "\n");
	temp.MakeFile("workspace/main.cpp", "\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	FakeClock clock;
	services.SetClock(clock.Get());
	UseClangdAt(services, temp.GetPath() / "absent.exe");

	const std::filesystem::path source = temp.GetPath() / "workspace" / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.Poll();
	clock.Now += Graphite::kIdleShutdown * 3;
	services.Poll();

	CHECK(services.IsRunningFor(source, Graphite::LanguageId::Cpp));
}

// A subfolder of a repository opened on its own: the server is rooted at the repository, above
// the open folder, and it is still the workspace's server.
TEST_CASE("A server rooted above the open folder is never shut down for being idle")
{
	TempDirectory temp;
	temp.MakeFile("repo/compile_flags.txt", "\n");
	temp.MakeFile("repo/sub/main.cpp", "\n");

	Graphite::LanguageServices services{temp.GetPath() / "repo" / "sub"};
	FakeClock clock;
	services.SetClock(clock.Get());
	UseClangdAt(services, temp.GetPath() / "absent.exe");

	const std::filesystem::path source = temp.GetPath() / "repo" / "sub" / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.Poll();
	clock.Now += Graphite::kIdleShutdown * 3;
	services.Poll();

	CHECK(services.IsRunningFor(source, Graphite::LanguageId::Cpp));
}

TEST_CASE("A server holding an open document is never shut down for being idle")
{
	TempDirectory temp;
	temp.MakeFile("workspace/readme.txt", "\n");
	temp.MakeFile("elsewhere/compile_flags.txt", "\n");
	temp.MakeFile("elsewhere/loose.cpp", "int x;\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	FakeClock clock;
	services.SetClock(clock.Get());
	UseClangdAt(services, temp.GetPath() / "absent.exe");

	const std::filesystem::path loose = temp.GetPath() / "elsewhere" / "loose.cpp";

	services.EnsureStarted(loose, Graphite::LanguageId::Cpp);
	services.OpenDocument(loose, Graphite::LanguageId::Cpp, "int x;\n");
	services.Poll();
	clock.Now += Graphite::kIdleShutdown * 3;
	services.Poll();

	CHECK(services.IsRunningFor(loose, Graphite::LanguageId::Cpp));

	// And once it is closed, the delay starts from the close, not from when it was opened.
	services.CloseDocument(loose, Graphite::LanguageId::Cpp);
	services.Poll();
	clock.Now += std::chrono::seconds(1);
	services.Poll();
	CHECK(services.IsRunningFor(loose, Graphite::LanguageId::Cpp));
}

// A reaped server's name for a file goes with it: the header's server is gone, so the header is
// judged by its own directory again rather than by a root nothing runs at.
TEST_CASE("Reaping a server forgets the files it named")
{
	TempDirectory temp;
	temp.MakeFile("workspace/readme.txt", "\n");
	temp.MakeFile("elsewhere/compile_flags.txt", "\n");
	temp.MakeFile("elsewhere/loose.cpp", "int x;\n");
	temp.MakeFile("sdk/include/vector", "// header\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	FakeClock clock;
	AskedRoots asked;
	services.SetClock(clock.Get());
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Refusing());

	const std::filesystem::path loose = temp.GetPath() / "elsewhere" / "loose.cpp";
	const std::filesystem::path header = temp.GetPath() / "sdk" / "include" / "vector";

	services.EnsureStarted(loose, Graphite::LanguageId::Cpp);
	services.NoteNamed(header, loose, Graphite::LanguageId::Cpp);
	services.Poll();
	clock.Now += Graphite::kIdleShutdown + std::chrono::seconds(1);
	services.Poll();
	REQUIRE(asked.Roots.size() == 1);

	services.EnsureStarted(header, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 2);
	CHECK(asked.Roots[1] == temp.GetPath() / "sdk" / "include");
}

// The sticky key lives exactly as long as the document is open: a file closed and opened again
// resolves afresh, so a fallback root set in between is honoured.
TEST_CASE("Closing a document releases its server key so a reopened file resolves afresh")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{{}};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Refusing());

	const std::filesystem::path source = temp.GetPath() / "workspace" / "src" / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "x\n");
	REQUIRE(asked.Roots.size() == 1);
	CHECK(asked.Roots[0] == temp.GetPath() / "workspace" / "src");

	services.CloseDocument(source, Graphite::LanguageId::Cpp);
	services.SetFallbackRoot(temp.GetPath() / "workspace");

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "x\n");

	REQUIRE(asked.Roots.size() == 2);
	CHECK(asked.Roots[1] == temp.GetPath() / "workspace");
}

// Withdrawing trust from a server takes back what it named, the same as reaping it does.
TEST_CASE("Restricting a server forgets the files it named")
{
	TempDirectory temp;
	temp.MakeFile("project/compile_flags.txt", "-std=c++20\n");
	temp.MakeFile("project/main.cpp", "#include <vector>\n");
	temp.MakeFile("sdk/include/vector", "// header\n");

	Graphite::LanguageServices services{temp.GetPath() / "project"};
	std::vector<std::filesystem::path> asked;
	bool permitProject = true;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&](const std::filesystem::path& root) {
		asked.push_back(root);
		return permitProject;
	});

	const std::filesystem::path source = temp.GetPath() / "project" / "main.cpp";
	const std::filesystem::path header = temp.GetPath() / "sdk" / "include" / "vector";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.NoteNamed(header, source, Graphite::LanguageId::Cpp);
	REQUIRE(asked.size() == 1);

	permitProject = false;
	services.Revalidate();
	asked.clear();

	services.EnsureStarted(header, Graphite::LanguageId::Cpp);

	REQUIRE(asked.size() == 1);
	CHECK(asked[0] == temp.GetPath() / "sdk" / "include");
}

TEST_CASE("Destroying the services while a warm-up walk is running does not wait for the walk to finish")
{
	TempDirectory temp;

	// A tree with no source of any configured language, so nothing ends the walk early and
	// it has to visit every entry.
	for (int dir = 0; dir < 200; ++dir)
	{
		for (int file = 0; file < 100; ++file)
		{
			temp.MakeFile("d" + std::to_string(dir) + "/f" + std::to_string(file) + ".txt", "x");
		}
	}

	// What the walk costs when nothing stops it, measured on the same tree.
	const auto walkStart = std::chrono::steady_clock::now();
	std::size_t entries = 0;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(temp.GetPath()))
	{
		(void)entry;
		++entries;
	}
	const auto fullWalk = std::chrono::steady_clock::now() - walkStart;
	REQUIRE(entries >= 20000);

	std::chrono::steady_clock::duration closing{};
	{
		auto services = std::make_unique<Graphite::LanguageServices>(temp.GetPath());
		services->WarmUp(temp.GetPath(), false);

		// Let the worker get going, then close the way the app does.
		std::this_thread::sleep_for(std::chrono::milliseconds(5));

		const auto closeStart = std::chrono::steady_clock::now();
		services.reset();
		closing = std::chrono::steady_clock::now() - closeStart;
	}

	// The walk visits each entry and tests it against the ignore rules, so it costs more
	// than the bare iteration above; closing in under half of that bare cost means it stopped
	// early rather than finishing.
	CHECK(closing < fullWalk / 2);
}

namespace {
	// Hands LanguageServices clients with no process behind them, and keeps a pointer to the
	// newest one so a test can kill it or finish its handshake. The pointer is only good until
	// the next restart destroys that client, which is why it is re-read after every Poll.
	struct ClientFactorySpy
	{
		std::size_t Made = 0;
		Graphite::LspClient* Latest = nullptr;
		Graphite::ServerLaunch LastLaunch;
		std::filesystem::path LastRoot;

		Graphite::LanguageServices::ClientFactory Get()
		{
			return [this](const Graphite::ServerLaunch& launch, const std::filesystem::path& root) { return Make(launch, root); };
		}

		Core::Scope<Graphite::LspClient> Make(const Graphite::ServerLaunch& launch, const std::filesystem::path& root)
		{
			LastLaunch = launch;
			LastRoot = root;
			Core::Scope<Graphite::LspClient> client = Core::CreateScope<Graphite::LspClient>(launch, root, Graphite::LspClient::WithoutServer{});
			Latest = client.get();
			++Made;
			return client;
		}

		// Refuses the handshake, which is the shortest way a client without a process reports
		// Failed: the same state a crashed clangd's reader leaves behind.
		void Kill() const
		{
			Latest->Deliver(R"({"jsonrpc":"2.0","id":1,"error":{"code":-32603,"message":"gone"}})");
		}

		void Answer() const
		{
			Latest->Deliver(R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})");
		}
	};

	// A services object whose C++ server is built by `spy`, started for `source` under `temp`.
	// The binary must exist for StartServer to get as far as the factory; nothing ever runs it.
	void StartWithSpy(Graphite::LanguageServices& services, TempDirectory& temp,
					  ClientFactorySpy& spy, FakeClock& clock,
					  const std::filesystem::path& source)
	{
		temp.MakeFile("stand-in-clangd.exe", "");

		services.SetClock(clock.Get());
		services.SetClientFactory(spy.Get());
		UseClangdAt(services,
									 temp.GetPath() / "stand-in-clangd.exe");
		services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	}

	bool AnySentContaining(const std::vector<std::string>& sent, std::string_view needle)
	{
		for (const std::string& message : sent)
		{
			if (message.find(needle) != std::string::npos)
			{
				return true;
			}
		}

		return false;
	}

	bool ContainsPath(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& wanted)
	{
		for (const std::filesystem::path& path : paths)
		{
			if (path == wanted)
			{
				return true;
			}
		}

		return false;
	}
}

namespace {

	Graphite::ServerChoice ChooseNothing(Graphite::LanguageId language)
	{
		return Graphite::ChooseServer(language, std::string{Graphite::kServerTurnedOff});
	}
}

TEST_CASE("A document opened while no server is chosen reaches the server chosen later with its edits")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetChoiceProvider(ChooseNothing);

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "int first;\n");
	services.ChangeDocument(source, Graphite::LanguageId::Cpp, "int edited;\n");

	CHECK(services.Holds(source, Graphite::LanguageId::Cpp));
	CHECK(spy.Made == 0);

	temp.MakeFile("stand-in-clangd.exe", "");
	UseClangdAt(services, temp.GetPath() / "stand-in-clangd.exe");
	services.Reconfigure(Graphite::LanguageId::Cpp);

	REQUIRE(spy.Made == 1);
	spy.Answer();
	const std::vector<std::string> sent = spy.Latest->TakeSent();

	CHECK(AnySentContaining(sent, "textDocument/didOpen"));
	CHECK(AnySentContaining(sent, "int edited;"));
	CHECK_FALSE(AnySentContaining(sent, "int first;"));
}

TEST_CASE("A document closed while no server is chosen is not replayed later")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetChoiceProvider(ChooseNothing);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "int closed;\n");
	services.CloseDocument(source, Graphite::LanguageId::Cpp);

	CHECK_FALSE(services.Holds(source, Graphite::LanguageId::Cpp));

	temp.MakeFile("stand-in-clangd.exe", "");
	UseClangdAt(services, temp.GetPath() / "stand-in-clangd.exe");
	services.Reconfigure(Graphite::LanguageId::Cpp);

	CHECK(spy.Made == 0);
}

TEST_CASE("Whether a language has a chosen server and offers switch header follows the choice")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	services.SetChoiceProvider(ChooseNothing);
	CHECK_FALSE(services.HasChosenServer(Graphite::LanguageId::Cpp));
	CHECK_FALSE(services.OffersSwitchSourceHeader(Graphite::LanguageId::Cpp));

	UseClangdAt(services, temp.GetPath() / "stand-in-clangd.exe");
	CHECK(services.HasChosenServer(Graphite::LanguageId::Cpp));
	CHECK(services.OffersSwitchSourceHeader(Graphite::LanguageId::Cpp));
	CHECK_FALSE(services.HasChosenServer(Graphite::LanguageId::PlainText));
	CHECK_FALSE(services.HasChosenServer(Graphite::LanguageId::CSharp));
}

TEST_CASE("A dead client is replaced through the factory and told every open document again")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	StartWithSpy(services, temp, spy, clock, source);
	REQUIRE(spy.Made == 1);

	services.OpenDocument(source, Graphite::LanguageId::Cpp, "int replayed;\n");

	spy.Kill();
	services.Poll();

	// The first restart waits for nothing: RestartBackoff(0) is zero.
	CHECK(spy.Made == 2);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).State ==
		  Graphite::ServerState::Starting);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).Restarts == 1);

	// The replay is held behind the new client's handshake, like anything else it is sent.
	spy.Answer();

	const std::vector<std::string> sent = spy.Latest->TakeSent();

	CHECK(AnySentContaining(sent, "textDocument/didOpen"));
	CHECK(AnySentContaining(sent, "int replayed;"));
}

TEST_CASE("A server that dies again waits out its backoff on the services clock before restarting")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	StartWithSpy(services, temp, spy, clock, source);

	spy.Kill();
	services.Poll();
	REQUIRE(spy.Made == 2);

	spy.Kill();
	services.Poll();

	// Noticed, but RestartBackoff(1) has not elapsed.
	CHECK(spy.Made == 2);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).State ==
		  Graphite::ServerState::Failed);

	clock.Now += Graphite::RestartBackoff(1) - std::chrono::milliseconds(1);
	services.Poll();
	CHECK(spy.Made == 2);

	clock.Now += std::chrono::milliseconds(1);
	services.Poll();
	CHECK(spy.Made == 3);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).Restarts == 2);
}

TEST_CASE("A server that keeps dying is left Failed after kMaximumRestarts restarts")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	StartWithSpy(services, temp, spy, clock, source);

	for (std::size_t death = 0; death < Graphite::kMaximumRestarts; ++death)
	{
		spy.Kill();
		services.Poll();

		// Far past any backoff, and well short of kStableAfter: a dead server is never up, so
		// the budget cannot refill however long this waits.
		clock.Now += std::chrono::seconds(30);
		services.Poll();
	}

	REQUIRE(spy.Made == 1 + Graphite::kMaximumRestarts);

	spy.Kill();

	for (int frame = 0; frame < 10; ++frame)
	{
		services.Poll();
		clock.Now += std::chrono::minutes(1);
	}

	CHECK(spy.Made == 1 + Graphite::kMaximumRestarts);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).State ==
		  Graphite::ServerState::Failed);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).Restarts ==
		  Graphite::kMaximumRestarts);
}

TEST_CASE("A restarted server that stays up for kStableAfter earns its restarts back")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	StartWithSpy(services, temp, spy, clock, source);

	spy.Kill();
	services.Poll();
	REQUIRE(spy.Made == 2);

	spy.Answer();
	services.Poll();
	REQUIRE(services.StatusFor(source, Graphite::LanguageId::Cpp).Restarts == 1);

	clock.Now += Graphite::kStableAfter - std::chrono::seconds(1);
	services.Poll();
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).Restarts == 1);

	clock.Now += std::chrono::seconds(1);
	services.Poll();
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).Restarts == 0);

	// Not killed again to watch the zero backoff come back: a client without a process can only
	// fail its handshake, and this one has finished it. What the restart path reads is
	// server.Restarts, and the status above is that number as Poll left it.
}

TEST_CASE("A message a server shows reaches TakeMessages once")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	StartWithSpy(services, temp, spy, clock, temp.GetPath() / "main.cpp");
	spy.Latest->Deliver(R"({"method":"window/showMessage","params":{"type":1,"message":"boom"}})");

	services.Poll();

	std::vector<Graphite::ServerMessage> messages;
	services.TakeMessages(messages);

	REQUIRE(messages.size() == 1);
	CHECK(messages[0].Text == "boom");

	std::vector<Graphite::ServerMessage> again;
	services.Poll();
	services.TakeMessages(again);

	CHECK(again.empty());
}

TEST_CASE("The warm-up still opens and closes a C++ file so clangd loads its compile database")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	temp.MakeFile("main.cpp", "int main() { return 0; }\n");
	temp.MakeFile("stand-in-clangd.exe", "");

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	UseClangdAt(services, temp.GetPath() / "stand-in-clangd.exe");

	services.WarmUp(temp.GetPath(), false);

	constexpr int kMaxAttempts = 500;

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	REQUIRE(spy.Made == 1);

	spy.Answer();

	const std::vector<std::string> sent = spy.Latest->TakeSent();

	CHECK(AnySentContaining(sent, "textDocument/didOpen"));
	CHECK(AnySentContaining(sent, "textDocument/didClose"));
}

TEST_CASE("A language whose server is turned off reports Off and starts nothing")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	services.SetChoiceProvider([](Graphite::LanguageId language) {
		return Graphite::ChooseServer(language, std::string{Graphite::kServerTurnedOff});
	});

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);

	CHECK_FALSE(services.IsRunningFor(source, Graphite::LanguageId::Cpp));
	CHECK_FALSE(services.OverallStatus().has_value());

	const Graphite::ServerStatus status = services.StatusFor(source, Graphite::LanguageId::Cpp);

	CHECK(status.State == Graphite::ServerState::Off);
	CHECK(status.Name == "C++");
}

TEST_CASE("The user's extra arguments and options reach the client that is built")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	temp.MakeFile("stand-in-clangd.exe", "");

	const nlohmann::json options{{"fallbackFlags", nlohmann::json::array({"-std=c++20"})}};
	const Graphite::ServerChoice choice{.Kind = Graphite::ServerChoiceKind::Chosen,
									   .Server = Graphite::FindServer("clangd"),
									   .Overrides = Graphite::ServerOverrides{.Binary = temp.GetPath() / "stand-in-clangd.exe",
																			 .ExtraArguments = {"--log=verbose"},
																			 .InitializationOptions = options,
																			 .Settings = nullptr}};

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetChoiceProvider([choice](Graphite::LanguageId) { return choice; });
	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	REQUIRE(spy.Made == 1);
	CHECK(spy.LastLaunch.Arguments == std::vector<std::string>{"--background-index", "--pch-storage=disk", "--log=verbose"});
	CHECK(spy.LastLaunch.InitializationOptions == options);
}

TEST_CASE("The choice is asked once per language until Reconfigure asks again")
{
	struct AskCounter
	{
		std::size_t Cpp = 0;
		std::size_t Other = 0;
	};

	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	AskCounter counter;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	services.SetChoiceProvider([&counter](Graphite::LanguageId language) {
		if (language == Graphite::LanguageId::Cpp)
		{
			++counter.Cpp;
		}
		else
		{
			++counter.Other;
		}

		return Graphite::ChooseServer(language, std::string{Graphite::kServerTurnedOff});
	});

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.StatusFor(source, Graphite::LanguageId::Cpp);
	services.StatusFor(Graphite::LanguageId::Cpp);

	CHECK(counter.Cpp == 1);
	CHECK(counter.Other == 0);

	services.Reconfigure(Graphite::LanguageId::Cpp);
	services.StatusFor(source, Graphite::LanguageId::Cpp);

	CHECK(counter.Cpp == 2);
}

TEST_CASE("Reconfigure moves every open document of the language to the server chosen now")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	StartWithSpy(services, temp, spy, clock, source);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "int moved;\n");

	temp.MakeFile("other-clangd.exe", "");
	UseClangdAt(services, temp.GetPath() / "other-clangd.exe");
	services.Reconfigure(Graphite::LanguageId::Cpp);

	REQUIRE(spy.Made == 2);
	CHECK(spy.LastLaunch.Binary == temp.GetPath() / "other-clangd.exe");
	CHECK(services.Holds(source, Graphite::LanguageId::Cpp));

	Graphite::DiagnosticsUpdate update;
	services.TakeDiagnostics(update);
	CHECK(ContainsPath(update.Closed, source));

	spy.Answer();
	const std::vector<std::string> sent = spy.Latest->TakeSent();

	CHECK(AnySentContaining(sent, "textDocument/didOpen"));
	CHECK(AnySentContaining(sent, "int moved;"));
}

TEST_CASE("Reconfigure answers what the old server still owed as failed")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	StartWithSpy(services, temp, spy, clock, source);
	spy.Answer();

	const Graphite::FilePosition where{source, 0, Graphite::Utf16Column{0}};
	const Graphite::LanguageServices::Ticket ticket = services.AskDefinition(source, Graphite::LanguageId::Cpp, where);

	services.Reconfigure(Graphite::LanguageId::Cpp);

	std::vector<Graphite::LanguageServices::Answer> answers;
	services.TakeAnswers(answers);

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].For == ticket);
	CHECK(answers[0].Failed);
}

TEST_CASE("Reconfigure to a server turned off stops the server and reports Off")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	StartWithSpy(services, temp, spy, clock, source);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "int dropped;\n");

	services.SetChoiceProvider([](Graphite::LanguageId language) {
		return Graphite::ChooseServer(language, std::string{Graphite::kServerTurnedOff});
	});
	services.Reconfigure(Graphite::LanguageId::Cpp);

	CHECK(spy.Made == 1);
	CHECK_FALSE(services.IsRunningFor(source, Graphite::LanguageId::Cpp));
	CHECK_FALSE(services.OverallStatus().has_value());
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).State == Graphite::ServerState::Off);

	Graphite::DiagnosticsUpdate update;
	services.TakeDiagnostics(update);
	CHECK(ContainsPath(update.Closed, source));
}

TEST_CASE("Reconfigure keeps a file reached through a definition on the root of the server that named it")
{
	TempDirectory temp;
	TempDirectory elsewhere;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	elsewhere.MakeFile("header.h", "");
	const std::filesystem::path header = elsewhere.GetPath() / "header.h";

	StartWithSpy(services, temp, spy, clock, source);
	services.NoteNamed(header, source, Graphite::LanguageId::Cpp);
	services.EnsureStarted(header, Graphite::LanguageId::Cpp);
	services.OpenDocument(header, Graphite::LanguageId::Cpp, "int header;");

	REQUIRE(spy.Made == 1);

	temp.MakeFile("other-clangd.exe", "");
	UseClangdAt(services, temp.GetPath() / "other-clangd.exe");
	services.Reconfigure(Graphite::LanguageId::Cpp);

	CHECK(spy.Made == 2);
	CHECK(spy.LastRoot == temp.GetPath());
	CHECK(services.Holds(header, Graphite::LanguageId::Cpp));
}

TEST_CASE("Reconfigure starts the workspace's server again even when no document was open on it")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	temp.MakeFile("main.cpp", "int main() { return 0; }\n");
	temp.MakeFile("stand-in-clangd.exe", "");
	temp.MakeFile("other-clangd.exe", "");

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	UseClangdAt(services, temp.GetPath() / "stand-in-clangd.exe");
	services.WarmUp(temp.GetPath(), false);

	constexpr int kMaxAttempts = 500;

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	REQUIRE(spy.Made == 1);

	UseClangdAt(services, temp.GetPath() / "other-clangd.exe");
	services.Reconfigure(Graphite::LanguageId::Cpp);

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made < 2; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	REQUIRE(spy.Made == 2);
	CHECK(spy.LastLaunch.Binary == temp.GetPath() / "other-clangd.exe");
}

TEST_CASE("Reconfigure does not warm a folder that is no longer the open one")
{
	TempDirectory folderA;
	TempDirectory folderB;
	Graphite::LanguageServices services{folderA.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	folderA.MakeFile("main.cpp", "int main() { return 0; }\n");
	folderA.MakeFile("stand-in-clangd.exe", "");
	folderA.MakeFile("other-clangd.exe", "");
	folderB.MakeFile("b.cpp", "int b;\n");

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	UseClangdAt(services, folderA.GetPath() / "stand-in-clangd.exe");
	services.WarmUp(folderA.GetPath(), false);

	constexpr int kMaxAttempts = 500;

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	REQUIRE(spy.Made == 1);

	services.SetFallbackRoot(folderB.GetPath());
	services.EnsureStarted(folderB.GetPath() / "b.cpp", Graphite::LanguageId::Cpp);

	REQUIRE(spy.Made == 2);

	UseClangdAt(services, folderA.GetPath() / "other-clangd.exe");
	services.Reconfigure(Graphite::LanguageId::Cpp);

	constexpr int kSettleAttempts = 100;

	for (int attempt = 0; attempt < kSettleAttempts; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(spy.Made == 2);
}

TEST_CASE("Replacing the choice provider asks the new one")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	g_AsksOfFirstProvider = 0;
	g_AsksOfSecondProvider = 0;

	services.SetChoiceProvider([](Graphite::LanguageId language) { return AnswerAsFirstProvider(language); });
	services.StatusFor(Graphite::LanguageId::Cpp);
	services.StatusFor(Graphite::LanguageId::Cpp);

	CHECK(g_AsksOfFirstProvider == 1);

	services.SetChoiceProvider([](Graphite::LanguageId language) { return AnswerAsSecondProvider(language); });
	services.StatusFor(Graphite::LanguageId::Cpp);

	CHECK(g_AsksOfFirstProvider == 1);
	CHECK(g_AsksOfSecondProvider == 1);
}

namespace {

	Graphite::LanguageServices::DotnetLocator DotnetAt(const std::filesystem::path& executable, const std::string& runtimeVersion)
	{
		return [executable, runtimeVersion] {
			return std::vector<Graphite::DotnetInstallation>{
				Graphite::DotnetInstallation{.Executable = executable, .RuntimeVersions = {runtimeVersion}}};
		};
	}

	Graphite::ServerChoice ChooseCsharpLs(Graphite::LanguageId language)
	{
		return Graphite::ChooseServer(language, std::string{"csharp-ls"});
	}
}

TEST_CASE("A C# server installed by Graphite starts through dotnet from its install")
{
	TempDirectory temp;
	const std::filesystem::path version = InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider(ChooseCsharpLs);

	services.EnsureStarted(temp.GetPath() / "Demo" / "Program.cs", Graphite::LanguageId::CSharp);

	REQUIRE(spy.Made == 1);
	CHECK(spy.LastLaunch.Binary == temp.GetPath() / "dotnet" / "dotnet.exe");
	REQUIRE_FALSE(spy.LastLaunch.Arguments.empty());
	CHECK(spy.LastLaunch.Arguments[0] == Core::ToUtf8(version / kCsharpLsAssembly));
}

TEST_CASE("A C# server without its runtime reads MissingRuntime and starts nothing")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	const std::filesystem::path source = temp.GetPath() / "Demo" / "Program.cs";

	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "8.0.11"));
	services.SetChoiceProvider(ChooseCsharpLs);

	services.EnsureStarted(source, Graphite::LanguageId::CSharp);

	CHECK(spy.Made == 0);
	CHECK(services.StatusFor(source, Graphite::LanguageId::CSharp).State == Graphite::ServerState::MissingRuntime);
	CHECK(services.StatusFor(source, Graphite::LanguageId::CSharp).Detail == "needs .NET 10");
	CHECK(services.OverallStatus()->State == Graphite::ServerState::MissingRuntime);
}