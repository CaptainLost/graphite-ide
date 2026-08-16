#include <doctest/doctest.h>

#include "Lang/Server/LanguageServices.h"
#include "TempDirectory.h"

#include <chrono>

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
