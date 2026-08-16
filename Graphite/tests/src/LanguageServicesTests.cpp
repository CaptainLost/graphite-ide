#include <doctest/doctest.h>

#include "Lang/Server/LanguageServices.h"
#include "TempDirectory.h"

#include <chrono>
#include <thread>

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
