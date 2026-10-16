#include <doctest/doctest.h>

#include "FakeLanguageServer.h"
#include "InstalledServers.h"
#include "Language/Install/InstalledServer.h"
#include "Language/Server/DotnetRuntime.h"
#include "Language/Server/LanguageServices.h"
#include "Language/Server/LspProtocol.h"
#include "Language/Server/ServerChoice.h"
#include "LspMessages.h"
#include "TempDirectory.h"

#include "Core/FileSystem/FileSystem.h"

#include <chrono>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace {
	constexpr bool kDoNotRespectGitIgnore = false;
	constexpr int kMaxAttempts = 500;
	const std::string kNoContent;
	const std::string_view kNoItems;
	const Graphite::FilePosition kNowhere{};
	const std::filesystem::path kNoFolder;
	constexpr std::chrono::milliseconds kWorkerStartDelay{5};
	constexpr int kHalf = 2;
	constexpr int kSecondsSinceClockStart = 1000;
	constexpr int kDirectoryCount = 200;
	constexpr int kFilesPerDirectory = 100;
	constexpr std::size_t kMinimumEntries = 20000;

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

	struct AskedRoots
	{
		std::vector<std::filesystem::path> Roots;
		bool Permitting = false;

		bool Ask(const std::filesystem::path& root)
		{
			Roots.push_back(root);
			return Permitting;
		}

		Graphite::LanguageServices::RootPermission Recording()
		{
			return std::bind_front(&AskedRoots::Ask, this);
		}
	};

	Graphite::ServerChoice TurnedOffFor(Graphite::LanguageId language)
	{
		return Graphite::ChooseServer(language, std::string{Graphite::kServerTurnedOff});
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

TEST_CASE("Every status names the language it is about")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.EnsureStarted(source, Graphite::LanguageId::Cpp);

	CHECK(services.StatusFor(Graphite::LanguageId::Cpp).Language == Graphite::LanguageId::Cpp);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).Language == Graphite::LanguageId::Cpp);
	REQUIRE(services.OverallStatus().has_value());
	CHECK(services.OverallStatus()->Language == Graphite::LanguageId::Cpp);
	CHECK(services.StatusFor(Graphite::LanguageId::CSharp).Language == Graphite::LanguageId::CSharp);
}

TEST_CASE("A language turned off reads Off for that language")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	services.SetChoiceProvider([](Graphite::LanguageId language) { return Graphite::ChooseServer(language, std::string{Graphite::kServerTurnedOff}); });

	const Graphite::ServerStatus status = services.StatusFor(temp.GetPath() / "Program.cs", Graphite::LanguageId::CSharp);

	CHECK(status.State == Graphite::ServerState::Off);
	CHECK(status.Language == Graphite::LanguageId::CSharp);
}

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

TEST_CASE("Opening a folder starts a server for a language the tree contains")
{
	TempDirectory temp;

	temp.MakeFile("notes.txt", "nothing to see\n");
	temp.MakeFile("src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, temp.GetPath() / "absent.exe");

	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

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

TEST_CASE("A server started by opening a folder is reportable with no document of its language")
{
	TempDirectory temp;

	temp.MakeFile("src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{temp.GetPath()};
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

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

	CHECK_FALSE(services.OverallStatus().has_value());
}

TEST_CASE("A folder holding no source of a configured language starts nothing")
{
	TempDirectory temp;

	temp.MakeFile("notes.txt", "nothing to see\n");
	temp.MakeFile("data/readme.md", "# nothing\n");

	Graphite::LanguageServices services{temp.GetPath()};
	UseClangdAt(services, temp.GetPath() / "absent.exe");

	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

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
	CHECK(Graphite::RestartBackoff(0) == std::chrono::seconds(0));
	CHECK(Graphite::RestartBackoff(1) == std::chrono::seconds(2));
	CHECK(Graphite::RestartBackoff(2) == std::chrono::seconds(4));
}

TEST_CASE("A server may restart until it has used up kMaximumRestarts")
{
	CHECK(Graphite::MayRestart(0));
	CHECK(Graphite::MayRestart(2));
	CHECK_FALSE(Graphite::MayRestart(3));
	CHECK_FALSE(Graphite::MayRestart(4));
}

TEST_CASE("The restart budget refills only after the server has stayed up for kStableAfter")
{
	using Point = std::chrono::steady_clock::time_point;
	const Point since{std::chrono::seconds(kSecondsSinceClockStart)};

	CHECK_FALSE(Graphite::RestartBudgetRefilled(std::nullopt, since + std::chrono::hours(1)));

	CHECK_FALSE(Graphite::RestartBudgetRefilled(since, since));
	CHECK_FALSE(Graphite::RestartBudgetRefilled(since, since + Graphite::kStableAfter - std::chrono::seconds(1)));

	CHECK(Graphite::RestartBudgetRefilled(since, since + Graphite::kStableAfter));
	CHECK(Graphite::RestartBudgetRefilled(since, since + std::chrono::hours(1)));
}

TEST_CASE("Tickets are unique across two different servers")
{
	TempDirectory tempA;
	TempDirectory tempB;

	Graphite::LanguageServices services{tempA.GetPath()};
	UseClangdAt(services, tempA.GetPath() / "absent.exe");

	const Graphite::LanguageServices::Ticket first =
		services.AskDefinition(tempA.GetPath() / "a.cpp", Graphite::LanguageId::Cpp, kNowhere);
	const Graphite::LanguageServices::Ticket second =
		services.AskDefinition(tempB.GetPath() / "b.cpp", Graphite::LanguageId::Cpp, kNowhere);

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
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};

	UseClangdAt(services, "C:\\Windows\\System32\\where.exe");
	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	const Graphite::LanguageServices::Ticket ticket =
		services.AskSymbols(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

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

	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

	for (int attempt = 0; attempt < 50; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(services.StatusFor(Graphite::LanguageId::Cpp).State == Graphite::ServerState::NotConfigured);
}

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

	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

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

TEST_CASE("A document's status is its own root's, not another root's of the same language")
{
	TempDirectory temp;

	temp.MakeFile("other/compile_flags.txt", kNoContent);
	temp.MakeFile("work/compile_flags.txt", kNoContent);

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

TEST_CASE("A trusted root's server outranks a restricted one for the workspace")
{
	TempDirectory temp;

	temp.MakeFile("other/compile_flags.txt", kNoContent);
	temp.MakeFile("work/compile_flags.txt", kNoContent);

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

TEST_CASE("A file with no marker inside the fallback root resolves to the fallback root")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	AskedRoots asked;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Recording());

	services.EnsureStarted(temp.GetPath() / "workspace" / "src" / "main.cpp", Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 1);
	CHECK(asked.Roots.front() == temp.GetPath() / "workspace");
}

TEST_CASE("A file with no marker outside the fallback root resolves to its own directory")
{
	TempDirectory temp;
	temp.MakeFile("workspace/readme.txt", "\n");
	temp.MakeFile("elsewhere/loose.cpp", "int x;\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	AskedRoots asked;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Recording());

	services.EnsureStarted(temp.GetPath() / "elsewhere" / "loose.cpp", Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 1);
	CHECK(asked.Roots.front() == temp.GetPath() / "elsewhere");
}

TEST_CASE("A file with no marker and no fallback root resolves to its own directory")
{
	TempDirectory temp;
	temp.MakeFile("loose/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{kNoFolder};
	AskedRoots asked;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Recording());

	services.EnsureStarted(temp.GetPath() / "loose" / "main.cpp", Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 1);
	CHECK(asked.Roots.front() == temp.GetPath() / "loose");
}

TEST_CASE("Changing the fallback root re-resolves a directory already seen")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{kNoFolder};
	AskedRoots asked;

	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Recording());

	const std::filesystem::path source = temp.GetPath() / "workspace" / "src" / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.SetFallbackRoot(temp.GetPath() / "workspace");
	services.EnsureStarted(source, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 2);
	CHECK(asked.Roots[0] == temp.GetPath() / "workspace" / "src");
	CHECK(asked.Roots[1] == temp.GetPath() / "workspace");
}

TEST_CASE("An open document keeps its server when the fallback root moves")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{kNoFolder};
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

	Graphite::LanguageServices services{kNoFolder};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Recording());

	const std::filesystem::path source = temp.GetPath() / "workspace" / "src" / "main.cpp";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "x\n");
	services.SetFallbackRoot(temp.GetPath() / "workspace");

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
	services.SetRootPermission(asked.Recording());

	std::wstring lowered = (temp.GetPath() / "Workspace" / "Src" / "main.cpp").native();
	for (wchar_t& character : lowered)
	{
		character = static_cast<wchar_t>(towlower(character));
	}

	services.EnsureStarted(std::filesystem::path{lowered}, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 1);
	CHECK(asked.Roots.front() == temp.GetPath() / "Workspace");
}

TEST_CASE("A markerless file a server named resolves to that server's root")
{
	TempDirectory temp;
	temp.MakeFile("project/compile_flags.txt", "-std=c++20\n");
	temp.MakeFile("project/main.cpp", "#include <vector>\n");
	temp.MakeFile("sdk/include/vector", "// header\n");

	Graphite::LanguageServices services{temp.GetPath() / "project"};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Recording());

	const std::filesystem::path source = temp.GetPath() / "project" / "main.cpp";
	const std::filesystem::path header = temp.GetPath() / "sdk" / "include" / "vector";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.NoteNamed(header, source, Graphite::LanguageId::Cpp);
	services.EnsureStarted(header, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 1);
	CHECK(asked.Roots.front() == temp.GetPath() / "project");
}

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
	services.SetRootPermission(asked.Recording());

	const std::filesystem::path source = temp.GetPath() / "project" / "main.cpp";
	const std::filesystem::path header = temp.GetPath() / "library" / "lib.h";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.NoteNamed(header, source, Graphite::LanguageId::Cpp);
	services.EnsureStarted(header, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 2);
	CHECK(asked.Roots[1] == temp.GetPath() / "library");
}

TEST_CASE("A markerless file inside the open folder stays the folder's when another server names it")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/shared.h", "\n");
	temp.MakeFile("library/compile_flags.txt", "-std=c++20\n");
	temp.MakeFile("library/lib.cpp", "\n");

	Graphite::LanguageServices services{temp.GetPath() / "workspace"};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Recording());

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

	services.CloseDocument(loose, Graphite::LanguageId::Cpp);
	services.Poll();
	clock.Now += std::chrono::seconds(1);
	services.Poll();
	CHECK(services.IsRunningFor(loose, Graphite::LanguageId::Cpp));
}

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
	services.SetRootPermission(asked.Recording());

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

TEST_CASE("Closing a document releases its server key so a reopened file resolves afresh")
{
	TempDirectory temp;
	temp.MakeFile("workspace/src/main.cpp", "int main() { return 0; }\n");

	Graphite::LanguageServices services{kNoFolder};
	AskedRoots asked;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Recording());

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

TEST_CASE("Restricting a server forgets the files it named")
{
	TempDirectory temp;
	temp.MakeFile("project/compile_flags.txt", "-std=c++20\n");
	temp.MakeFile("project/main.cpp", "#include <vector>\n");
	temp.MakeFile("sdk/include/vector", "// header\n");

	Graphite::LanguageServices services{temp.GetPath() / "project"};
	AskedRoots asked;
	asked.Permitting = true;
	UseClangdAt(services, temp.GetPath() / "absent.exe");
	services.SetRootPermission(asked.Recording());

	const std::filesystem::path source = temp.GetPath() / "project" / "main.cpp";
	const std::filesystem::path header = temp.GetPath() / "sdk" / "include" / "vector";

	services.EnsureStarted(source, Graphite::LanguageId::Cpp);
	services.NoteNamed(header, source, Graphite::LanguageId::Cpp);
	REQUIRE(asked.Roots.size() == 1);

	asked.Permitting = false;
	services.Revalidate();
	asked.Roots.clear();

	services.EnsureStarted(header, Graphite::LanguageId::Cpp);

	REQUIRE(asked.Roots.size() == 1);
	CHECK(asked.Roots[0] == temp.GetPath() / "sdk" / "include");
}

TEST_CASE("Destroying the services while a warm-up walk is running does not wait for the walk to finish")
{
	TempDirectory temp;

	for (int directory = 0; directory < kDirectoryCount; ++directory)
	{
		for (int file = 0; file < kFilesPerDirectory; ++file)
		{
			temp.MakeFile("d" + std::to_string(directory) + "/f" + std::to_string(file) + ".txt", "x");
		}
	}

	const std::chrono::steady_clock::time_point walkStart = std::chrono::steady_clock::now();
	std::size_t entries = 0;
	for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(temp.GetPath()))
	{
		(void)entry;
		++entries;
	}
	const std::chrono::steady_clock::duration fullWalk = std::chrono::steady_clock::now() - walkStart;
	REQUIRE(entries >= kMinimumEntries);

	std::chrono::steady_clock::duration closing{};
	{
		auto services = std::make_unique<Graphite::LanguageServices>(temp.GetPath());
		services->WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

		std::this_thread::sleep_for(kWorkerStartDelay);

		const std::chrono::steady_clock::time_point closeStart = std::chrono::steady_clock::now();
		services.reset();
		closing = std::chrono::steady_clock::now() - closeStart;
	}

	CHECK(closing < fullWalk / kHalf);
}

namespace {
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

		void Kill() const
		{
			Latest->Deliver(R"({"jsonrpc":"2.0","id":1,"error":{"code":-32603,"message":"gone"}})");
		}

		void Answer() const
		{
			Latest->Deliver(R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{}}})");
		}
	};

	void StartWithSpy(Graphite::LanguageServices& services, TempDirectory& temp,
					  ClientFactorySpy& spy, FakeClock& clock,
					  const std::filesystem::path& source)
	{
		temp.MakeFile("stand-in-clangd.exe", kNoContent);

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

TEST_CASE("Starting a server that needs no runtime looks for no .NET")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	std::size_t lookups = 0;

	services.SetDotnetLocator([&lookups] {
		++lookups;
		return std::vector<Graphite::DotnetInstallation>{};
	});

	StartWithSpy(services, temp, spy, clock, temp.GetPath() / "main.cpp");

	REQUIRE(spy.Made == 1);
	CHECK(lookups == 0);
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

	temp.MakeFile("stand-in-clangd.exe", kNoContent);
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

	temp.MakeFile("stand-in-clangd.exe", kNoContent);
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

	CHECK(spy.Made == 2);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).State ==
		  Graphite::ServerState::Starting);
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).Restarts == 1);

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
	temp.MakeFile("stand-in-clangd.exe", kNoContent);

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	UseClangdAt(services, temp.GetPath() / "stand-in-clangd.exe");

	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

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

	services.SetChoiceProvider(TurnedOffFor);

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

	temp.MakeFile("stand-in-clangd.exe", kNoContent);

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

		Graphite::ServerChoice Answer(Graphite::LanguageId language)
		{
			if (language == Graphite::LanguageId::Cpp)
			{
				++Cpp;
			}
			else
			{
				++Other;
			}

			return Graphite::ChooseServer(language, std::string{Graphite::kServerTurnedOff});
		}
	};

	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	AskCounter counter;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	services.SetChoiceProvider(std::bind_front(&AskCounter::Answer, &counter));

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

	temp.MakeFile("other-clangd.exe", kNoContent);
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

	services.SetChoiceProvider(TurnedOffFor);
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

	elsewhere.MakeFile("header.h", kNoContent);
	const std::filesystem::path header = elsewhere.GetPath() / "header.h";

	StartWithSpy(services, temp, spy, clock, source);
	services.NoteNamed(header, source, Graphite::LanguageId::Cpp);
	services.EnsureStarted(header, Graphite::LanguageId::Cpp);
	services.OpenDocument(header, Graphite::LanguageId::Cpp, "int header;");

	REQUIRE(spy.Made == 1);

	temp.MakeFile("other-clangd.exe", kNoContent);
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
	temp.MakeFile("stand-in-clangd.exe", kNoContent);
	temp.MakeFile("other-clangd.exe", kNoContent);

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	UseClangdAt(services, temp.GetPath() / "stand-in-clangd.exe");
	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

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
	folderA.MakeFile("stand-in-clangd.exe", kNoContent);
	folderA.MakeFile("other-clangd.exe", kNoContent);
	folderB.MakeFile("b.cpp", "int b;\n");

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	UseClangdAt(services, folderA.GetPath() / "stand-in-clangd.exe");
	services.WarmUp(folderA.GetPath(), kDoNotRespectGitIgnore);

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

TEST_CASE("A server configured as an assembly starts through dotnet though its definition names no runtime")
{
	TempDirectory temp;
	const std::filesystem::path assembly = temp.MakeFile("tools/clangd.dll", kNoContent);
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "8.0.11"));
	UseClangdAt(services, assembly);

	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	REQUIRE(spy.Made == 1);
	CHECK(spy.LastLaunch.Binary == temp.GetPath() / "dotnet" / "dotnet.exe");
	REQUIRE_FALSE(spy.LastLaunch.Arguments.empty());
	CHECK(spy.LastLaunch.Arguments[0] == Core::ToUtf8(assembly));
}

TEST_CASE("A server Graphite is uninstalling is not started and reads not installed")
{
	TempDirectory temp;
	InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	const std::filesystem::path source = temp.GetPath() / "Demo" / "Program.cs";

	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider(ChooseCsharpLs);
	services.SetUninstallingQuery([](std::string_view serverId) { return serverId == "csharp-ls"; });

	services.EnsureStarted(source, Graphite::LanguageId::CSharp);

	CHECK(spy.Made == 0);
	CHECK(services.StatusFor(source, Graphite::LanguageId::CSharp).State == Graphite::ServerState::NotInstalled);
	CHECK(services.StatusFor(source, Graphite::LanguageId::CSharp).Detail == "it is being uninstalled");
}

TEST_CASE("Choosing a server for the first time starts it for the open folder")
{
	TempDirectory temp;
	temp.MakeFile("Demo/Program.cs", "class Program {}\n");
	InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	bool chosen = false;

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider([&chosen](Graphite::LanguageId language) { return chosen ? ChooseCsharpLs(language) : Graphite::ChooseServer(language, std::nullopt); });
	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

	constexpr int kSettleAttempts = 50;

	for (int attempt = 0; attempt < kSettleAttempts; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	REQUIRE(spy.Made == 0);

	chosen = true;
	services.Reconfigure(Graphite::LanguageId::CSharp);

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(spy.Made == 1);
	CHECK(spy.LastRoot == temp.GetPath());
}

TEST_CASE("A running server's installed files cannot be removed until it stops")
{
	TempDirectory temp;
	const std::filesystem::path version = InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	std::string chosen = "csharp-ls";

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider([&chosen](Graphite::LanguageId language) { return Graphite::ChooseServer(language, chosen); });

	services.EnsureStarted(temp.GetPath() / "Program.cs", Graphite::LanguageId::CSharp);
	REQUIRE(spy.Made == 1);

	CHECK_FALSE(Graphite::RemoveDirectoryWhole(version));

	chosen = std::string{Graphite::kServerTurnedOff};
	services.Reconfigure(Graphite::LanguageId::CSharp);

	CHECK(Graphite::RemoveDirectoryWhole(version));
}

TEST_CASE("A server is started with the selected solution when it lies inside its root")
{
	TempDirectory temp;
	temp.MakeFile("Demo.slnx", kNoContent);
	temp.MakeFile("Demo/Program.cs", kNoContent);
	InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;

	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider(ChooseCsharpLs);
	services.SetSelectedSolution(temp.GetPath() / "Demo.slnx");

	services.EnsureStarted(temp.GetPath() / "Demo" / "Program.cs", Graphite::LanguageId::CSharp);

	REQUIRE(spy.Made == 1);
	CHECK(spy.LastLaunch.Solution == temp.GetPath() / "Demo.slnx");
}

namespace {
	Graphite::ServerChoice ChooseRoslynAt(Graphite::LanguageId language, const std::filesystem::path& binary)
	{
		if (language != Graphite::LanguageId::CSharp)
		{
			return Graphite::ChooseServer(language, std::nullopt);
		}

		Graphite::ServerChoice choice = Graphite::ChooseServer(language, std::string{"roslyn"});
		choice.Overrides.Binary = binary;
		return choice;
	}
}

TEST_CASE("Selecting another solution restarts a server that follows it on the new one")
{
	TempDirectory temp;
	const std::filesystem::path roslyn = temp.MakeFile("tools/roslyn-language-server.exe", kNoContent);
	temp.MakeFile("A.slnx", kNoContent);
	temp.MakeFile("B.slnx", kNoContent);
	const std::filesystem::path source = temp.MakeFile("Demo/Program.cs", "class Program { }\n");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;

	services.SetClientFactory(spy.Get());
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider([roslyn](Graphite::LanguageId language) { return ChooseRoslynAt(language, roslyn); });
	services.SetSelectedSolution(temp.GetPath() / "A.slnx");

	services.EnsureStarted(source, Graphite::LanguageId::CSharp);
	services.OpenDocument(source, Graphite::LanguageId::CSharp, "class Program { }\n");
	REQUIRE(spy.Made == 1);
	CHECK(spy.LastLaunch.Solution == temp.GetPath() / "A.slnx");

	services.SetSelectedSolution(temp.GetPath() / "B.slnx");

	REQUIRE(spy.Made == 2);
	CHECK(spy.LastLaunch.Solution == temp.GetPath() / "B.slnx");

	spy.Answer();
	const std::vector<std::string> sent = spy.Latest->TakeSent();
	CHECK(AnySentContaining(sent, "solution/open"));
	CHECK(AnySentContaining(sent, "B.slnx"));
	CHECK(AnySentContaining(sent, "textDocument/didOpen"));

	services.SetSelectedSolution(temp.GetPath() / "B.slnx");
	CHECK(spy.Made == 2);
}

TEST_CASE("Selecting the same solution again or under a server that does not follow it restarts nothing")
{
	TempDirectory temp;
	temp.MakeFile("A.slnx", kNoContent);
	temp.MakeFile("B.slnx", kNoContent);
	InstallCsharpLsIn(temp.GetPath() / "servers");
	const std::filesystem::path source = temp.MakeFile("Demo/Program.cs", "class Program { }\n");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;

	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider(ChooseCsharpLs);
	services.SetSelectedSolution(temp.GetPath() / "A.slnx");

	services.EnsureStarted(source, Graphite::LanguageId::CSharp);
	REQUIRE(spy.Made == 1);

	services.SetSelectedSolution(temp.GetPath() / "A.slnx");
	services.SetSelectedSolution(temp.GetPath() / "B.slnx");

	CHECK(spy.Made == 1);
}

TEST_CASE("A language's chosen server is reported until it is turned off")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	std::string chosen = "csharp-ls";

	services.SetChoiceProvider([&chosen](Graphite::LanguageId language) { return Graphite::ChooseServer(language, chosen); });
	CHECK(services.ChosenServer(Graphite::LanguageId::CSharp) == Graphite::FindServer("csharp-ls"));

	chosen = std::string{Graphite::kServerTurnedOff};
	services.Reconfigure(Graphite::LanguageId::CSharp);
	CHECK(services.ChosenServer(Graphite::LanguageId::CSharp) == nullptr);
}

TEST_CASE("The command a language's server was started with is reported only once one runs")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	CHECK_FALSE(services.FoundCommandFor(Graphite::LanguageId::Cpp).has_value());

	StartWithSpy(services, temp, spy, clock, temp.GetPath() / "main.cpp");

	const std::optional<Graphite::ServerCommand> found = services.FoundCommandFor(Graphite::LanguageId::Cpp);

	REQUIRE(found.has_value());
	CHECK(found->Program == temp.GetPath() / "stand-in-clangd.exe");
	CHECK_FALSE(services.FoundCommandFor(Graphite::LanguageId::CSharp).has_value());
}

TEST_CASE("A server that was not found has no command to report")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;

	services.SetClientFactory(spy.Get());
	UseClangdAt(services, temp.GetPath() / "missing-clangd.exe");
	services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);

	CHECK(spy.Made == 0);
	CHECK_FALSE(services.FoundCommandFor(Graphite::LanguageId::Cpp).has_value());
}

TEST_CASE("Roslyn without a solution starts with the projects found below its root")
{
	TempDirectory temp;
	const std::filesystem::path roslyn = temp.MakeFile("tools/roslyn-language-server.exe", kNoContent);
	const std::filesystem::path app = temp.MakeFile("src/App/App.csproj", kNoContent);
	const std::filesystem::path library = temp.MakeFile("src/Library/Library.csproj", kNoContent);
	temp.MakeFile("src/App/bin/Debug/App.csproj", kNoContent);
	const std::filesystem::path source = temp.MakeFile("src/App/Program.cs", "class Program { }\n");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;

	services.SetClientFactory(spy.Get());
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider([roslyn](Graphite::LanguageId language) { return ChooseRoslynAt(language, roslyn); });

	services.EnsureStarted(source, Graphite::LanguageId::CSharp);
	services.OpenDocument(source, Graphite::LanguageId::CSharp, "class Program { }\n");

	CHECK(spy.Made == 0);
	CHECK(services.StatusFor(source, Graphite::LanguageId::CSharp).State == Graphite::ServerState::Starting);
	CHECK(services.StatusFor(source, Graphite::LanguageId::CSharp).Detail == "looking for projects");

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	REQUIRE(spy.Made == 1);
	CHECK(spy.LastLaunch.Projects == std::vector<std::filesystem::path>{app, library});

	std::filesystem::remove(library);
	spy.Kill();
	services.Poll();

	REQUIRE(spy.Made == 2);
	CHECK(spy.LastLaunch.Projects == std::vector<std::filesystem::path>{app, library});

	spy.Answer();
	const std::vector<std::string> sent = spy.Latest->TakeSent();
	CHECK(AnySentContaining(sent, "project/open"));
	CHECK(AnySentContaining(sent, "textDocument/didOpen"));
}

TEST_CASE("A project search overtaken by a newer one starts one client")
{
	TempDirectory temp;
	const std::filesystem::path roslyn = temp.MakeFile("tools/roslyn-language-server.exe", kNoContent);
	temp.MakeFile("src/App/App.csproj", kNoContent);
	const std::filesystem::path source = temp.MakeFile("src/App/Program.cs", "class Program { }\n");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;

	services.SetClientFactory(spy.Get());
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider([roslyn](Graphite::LanguageId language) { return ChooseRoslynAt(language, roslyn); });

	services.EnsureStarted(source, Graphite::LanguageId::CSharp);
	services.OpenDocument(source, Graphite::LanguageId::CSharp, "class Program { }\n");
	services.Reconfigure(Graphite::LanguageId::CSharp);

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	constexpr int kSettleAttempts = 30;

	for (int attempt = 0; attempt < kSettleAttempts; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(spy.Made == 1);
}

TEST_CASE("A document changed during the project search reaches the first client with its changed text")
{
	TempDirectory temp;
	const std::filesystem::path roslyn = temp.MakeFile("tools/roslyn-language-server.exe", kNoContent);
	temp.MakeFile("src/App/App.csproj", kNoContent);
	const std::filesystem::path source = temp.MakeFile("src/App/Program.cs", "class Program { }\n");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;

	services.SetClientFactory(spy.Get());
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider([roslyn](Graphite::LanguageId language) { return ChooseRoslynAt(language, roslyn); });

	services.EnsureStarted(source, Graphite::LanguageId::CSharp);
	services.OpenDocument(source, Graphite::LanguageId::CSharp, "class Program { }\n");
	services.ChangeDocument(source, Graphite::LanguageId::CSharp, "class Edited { }\n");

	REQUIRE(spy.Made == 0);

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	REQUIRE(spy.Made == 1);

	spy.Answer();
	const std::vector<std::string> sent = spy.Latest->TakeSent();
	CHECK(AnySentContaining(sent, "textDocument/didOpen"));
	CHECK(AnySentContaining(sent, "class Edited"));
	CHECK_FALSE(AnySentContaining(sent, "class Program"));
}

TEST_CASE("The warm-up starts a solution-rooted server at the selected solution's folder")
{
	TempDirectory temp;
	temp.MakeFile("app/App.slnx", kNoContent);
	temp.MakeFile("app/Alpha/Alpha.slnx", kNoContent);
	temp.MakeFile("app/Alpha/Sample.cs", "class Sample { }\n");
	temp.MakeFile("app/src/Program.cs", "class Program { }\n");
	InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider(ChooseCsharpLs);
	services.SetSelectedSolution(temp.GetPath() / "app" / "App.slnx");

	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	constexpr int kSettleAttempts = 20;

	for (int attempt = 0; attempt < kSettleAttempts; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(spy.Made == 1);
	CHECK(spy.LastRoot == temp.GetPath() / "app");
}

TEST_CASE("Selecting a solution after the warm-up warms the server for its folder")
{
	TempDirectory temp;
	temp.MakeFile("outer/Outer.slnx", kNoContent);
	temp.MakeFile("outer/inner/Inner.slnx", kNoContent);
	temp.MakeFile("outer/inner/src/Program.cs", "class Program { }\n");
	InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider(ChooseCsharpLs);

	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

	constexpr int kSettleAttempts = 50;

	for (int attempt = 0; attempt < kSettleAttempts; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	REQUIRE(spy.Made == 0);

	services.SetSelectedSolution(temp.GetPath() / "outer" / "inner" / "Inner.slnx");

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(spy.Made == 1);
	CHECK(spy.LastRoot == temp.GetPath() / "outer" / "inner");
}

TEST_CASE("With no solution selected the warm-up starts the server of a solution below the root")
{
	TempDirectory temp;
	temp.MakeFile("src/App.sln", kNoContent);
	temp.MakeFile("src/App/Program.cs", "class Program { }\n");
	InstallCsharpLsIn(temp.GetPath() / "servers");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;

	services.SetClock(clock.Get());
	services.SetClientFactory(spy.Get());
	services.SetServersDirectory(temp.GetPath() / "servers");
	services.SetDotnetLocator(DotnetAt(temp.GetPath() / "dotnet" / "dotnet.exe", "10.0.10"));
	services.SetChoiceProvider(ChooseCsharpLs);

	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

	for (int attempt = 0; attempt < kMaxAttempts && spy.Made == 0; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(spy.Made == 1);
	CHECK(spy.LastRoot == temp.GetPath() / "src");
}

TEST_CASE("With no server chosen for C++ the warm-up starts nothing")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("main.cpp", "int main() { return 0; }\n");
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;

	services.SetClientFactory(spy.Get());
	services.WarmUp(temp.GetPath(), kDoNotRespectGitIgnore);

	constexpr int kSettleAttempts = 50;

	for (int attempt = 0; attempt < kSettleAttempts; ++attempt)
	{
		services.Poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	CHECK(spy.Made == 0);
	CHECK_FALSE(services.HasChosenServer(Graphite::LanguageId::Cpp));
	CHECK(services.StatusFor(source, Graphite::LanguageId::Cpp).State == Graphite::ServerState::NotConfigured);
}

namespace {
	struct FakeServerFactory
	{
		FakeLanguageServer Server;
		Graphite::LspClient* Latest = nullptr;

		Graphite::LanguageServices::ClientFactory Get()
		{
			return [this](const Graphite::ServerLaunch&, const std::filesystem::path& root) { return Make(root); };
		}

		Core::Scope<Graphite::LspClient> Make(const std::filesystem::path& root)
		{
			Core::Scope<Graphite::LspClient> client = Core::CreateScope<Graphite::LspClient>(FakeServerLaunch(Server), root);
			Latest = client.get();
			return client;
		}
	};

	void StartFakeServer(Graphite::LanguageServices& services, TempDirectory& temp, FakeServerFactory& factory)
	{
		temp.MakeFile("stand-in-clangd.exe", kNoContent);

		services.SetClientFactory(factory.Get());
		UseClangdAt(services, temp.GetPath() / "stand-in-clangd.exe");
		services.EnsureStarted(temp.GetPath() / "main.cpp", Graphite::LanguageId::Cpp);
	}

	std::optional<std::filesystem::path> TemporaryDirectoryOf(const Graphite::ServerLaunch& launch)
	{
		for (const Core::EnvironmentVariable& variable : launch.Environment)
		{
			if (variable.Name == "TMP")
			{
				return Core::FromUtf8(variable.Value);
			}
		}

		return std::nullopt;
	}
}

TEST_CASE("Closing the services gives a running server time to leave on its own")
{
	TempDirectory temp;
	FakeServerFactory factory{.Server = WriteFakeLanguageServer(temp, "obedient", FakeServerTemper::LeavesOnExit)};

	{
		Graphite::LanguageServices services{temp.GetPath()};
		StartFakeServer(services, temp, factory);

		REQUIRE(factory.Latest != nullptr);
		REQUIRE(WaitUntilAnswered(*factory.Latest));
	}

	CHECK(std::filesystem::exists(factory.Server.ExitHeard));
}

TEST_CASE("Stopping a server that ignores exit does not hold up the caller for its grace")
{
	TempDirectory temp;
	FakeServerFactory factory{.Server = WriteFakeLanguageServer(temp, "stubborn", FakeServerTemper::IgnoresExit)};
	Graphite::LanguageServices services{temp.GetPath()};
	StartFakeServer(services, temp, factory);

	REQUIRE(factory.Latest != nullptr);
	REQUIRE(WaitUntilAnswered(*factory.Latest));

	const std::chrono::steady_clock::time_point stopStart = std::chrono::steady_clock::now();
	services.Reconfigure(Graphite::LanguageId::Cpp);

	CHECK(std::chrono::steady_clock::now() - stopStart < Graphite::LspClient::kShutdownGrace / 2);
}

TEST_CASE("A server keeps its temporary files in a directory claimed under the one the services were given")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path temporaryFiles = temp.GetPath() / "server-temp";

	services.SetTemporaryFilesDirectory(temporaryFiles);
	StartWithSpy(services, temp, spy, clock, temp.GetPath() / "main.cpp");

	REQUIRE(spy.Made == 1);
	const std::optional<std::filesystem::path> serverTemporaryDirectory = TemporaryDirectoryOf(spy.LastLaunch);
	REQUIRE(serverTemporaryDirectory.has_value());
	CHECK(serverTemporaryDirectory->parent_path() == temporaryFiles);
	CHECK(std::filesystem::is_directory(*serverTemporaryDirectory));
}

TEST_CASE("Giving the services a directory for temporary files clears what an ended Graphite left in it")
{
	TempDirectory temp;
	const std::filesystem::path leftover = temp.MakeFile("server-temp/3/preamble-11adb3.pch", "left by a killed server");

	{
		Graphite::LanguageServices services{temp.GetPath()};
		services.SetTemporaryFilesDirectory(temp.GetPath() / "server-temp");
	}

	CHECK_FALSE(std::filesystem::exists(leftover.parent_path()));
}

TEST_CASE("A restarted server pulls the documents shown in tabs and not the ones only consulted")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path shown = temp.GetPath() / "shown.cpp";
	const std::filesystem::path consulted = temp.GetPath() / "consulted.cpp";

	StartWithSpy(services, temp, spy, clock, shown);
	services.EnsureStarted(consulted, Graphite::LanguageId::Cpp);
	services.OpenDocument(shown, Graphite::LanguageId::Cpp, "int shown;\n", Graphite::DocumentRole::Shown);
	services.OpenDocument(consulted, Graphite::LanguageId::Cpp, "int consulted;\n");

	spy.Kill();
	services.Poll();
	REQUIRE(spy.Made == 2);

	spy.Latest->Deliver(std::string{kHandshakeOfferingPulls});
	const std::vector<nlohmann::json> pulls = MessagesFor(spy.Latest->TakeSent(), "textDocument/diagnostic");

	REQUIRE(pulls.size() == 1);
	CHECK(UriOf(pulls[0]) == Graphite::ToFileUri(shown));
}

TEST_CASE("A document opened in a tab before any server was chosen is pulled by the server chosen later")
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
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "int first;\n", Graphite::DocumentRole::Shown);

	temp.MakeFile("stand-in-clangd.exe", kNoContent);
	UseClangdAt(services, temp.GetPath() / "stand-in-clangd.exe");
	services.Reconfigure(Graphite::LanguageId::Cpp);
	REQUIRE(spy.Made == 1);

	spy.Latest->Deliver(std::string{kHandshakeOfferingPulls});
	const std::vector<nlohmann::json> pulls = MessagesFor(spy.Latest->TakeSent(), "textDocument/diagnostic");

	REQUIRE(pulls.size() == 1);
	CHECK(UriOf(pulls[0]) == Graphite::ToFileUri(source));
}

TEST_CASE("A pull's answer comes out of TakeRecolour as the path the document was opened under")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	StartWithSpy(services, temp, spy, clock, source);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "int x;\n", Graphite::DocumentRole::Shown);
	spy.Latest->Deliver(std::string{kHandshakeOfferingPulls});

	const std::vector<nlohmann::json> pulls = MessagesFor(spy.Latest->TakeSent(), "textDocument/diagnostic");
	REQUIRE(pulls.size() == 1);
	spy.Latest->Deliver(FullReport(IdOf(pulls[0]), kNoItems));

	services.Poll();

	std::vector<std::filesystem::path> recolour;
	services.TakeRecolour(recolour);
	REQUIRE(recolour.size() == 1);
	CHECK(recolour[0] == source);

	recolour.clear();
	services.TakeRecolour(recolour);
	CHECK(recolour.empty());
}

TEST_CASE("An answer reaches its asker under the asker's ticket with what the server said")
{
	TempDirectory temp;
	Graphite::LanguageServices services{temp.GetPath()};
	ClientFactorySpy spy;
	FakeClock clock;
	const std::filesystem::path source = temp.GetPath() / "main.cpp";

	StartWithSpy(services, temp, spy, clock, source);
	services.OpenDocument(source, Graphite::LanguageId::Cpp, "void run();\n");
	spy.Answer();

	const Graphite::LanguageServices::Ticket ticket = services.AskSymbols(source, Graphite::LanguageId::Cpp);
	const std::vector<nlohmann::json> asked = MessagesFor(spy.Latest->TakeSent(), "textDocument/documentSymbol");
	REQUIRE(asked.size() == 1);

	const nlohmann::json range = {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 10}}}};
	const nlohmann::json reply = {{"jsonrpc", "2.0"},
								  {"id", IdOf(asked[0])},
								  {"result", nlohmann::json::array({{{"name", "run"}, {"kind", 12}, {"range", range}, {"selectionRange", range}}})}};
	spy.Latest->Deliver(reply.dump());

	services.Poll();

	std::vector<Graphite::LanguageServices::Answer> answers;
	services.TakeAnswers(answers);

	REQUIRE(answers.size() == 1);
	CHECK(answers[0].For == ticket);
	CHECK_FALSE(answers[0].Failed);
	REQUIRE(answers[0].Declarations.size() == 1);
	CHECK(answers[0].Declarations[0].Name == "run");
}