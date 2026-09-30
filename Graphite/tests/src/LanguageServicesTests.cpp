#include <doctest/doctest.h>

#include "Lang/Server/LanguageServices.h"
#include "TempDirectory.h"

#include <chrono>
#include <cwctype>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp,
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp,
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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");

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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");

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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
	services.EnsureStarted(temp.GetPath() / "a.cpp", Graphite::LanguageId::Cpp);
	services.EnsureStarted(temp.GetPath() / "b.cpp", Graphite::LanguageId::Cpp);

	CHECK(services.StatusFor(Graphite::LanguageId::Cpp).Restarts == 0);
}

TEST_CASE("Restart backoff grows with the number of restarts already attempted")
{
	// Pure policy, checked without a process: the delay before restart attempt N is N*2 seconds,
	// so the first attempt after a death is immediate and each further one waits longer. This is
	// the half of the restart path Poll can exercise deterministically — the other half needs a
	// server that actually dies, which nothing on this machine can be relied on to provide.
	CHECK(Graphite::RestartBackoff(0) == std::chrono::seconds(0));
	CHECK(Graphite::RestartBackoff(1) == std::chrono::seconds(2));
	CHECK(Graphite::RestartBackoff(2) == std::chrono::seconds(4));
}

TEST_CASE("Tickets are unique across two different servers")
{
	TempDirectory tempA;
	TempDirectory tempB;

	Graphite::LanguageServices services{tempA.GetPath()};
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, tempA.GetPath() / "absent.exe");

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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, "C:\\Windows\\System32\\where.exe");
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp,
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");

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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&refusedRoot](const std::filesystem::path& root)
							   { return root != refusedRoot; });

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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&refusedRoot](const std::filesystem::path& root)
							   { return root != refusedRoot; });

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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&asked](const std::filesystem::path& root)
							   {
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&asked](const std::filesystem::path& root)
							   {
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&asked](const std::filesystem::path& root)
							   {
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

	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
	services.SetRootPermission([&asked](const std::filesystem::path& root)
							   {
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
			return [this](const std::filesystem::path& root)
			{
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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");

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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");

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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");

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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");

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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");

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
	services.SetConfiguredBinary(Graphite::LanguageId::Cpp, temp.GetPath() / "absent.exe");
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
