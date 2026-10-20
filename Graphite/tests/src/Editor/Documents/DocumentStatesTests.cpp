#include <doctest/doctest.h>

#include "Editor/Documents/DocumentStates.h"
#include "Editor/Server/DocumentServerPort.h"
#include "Editor/Server/LspAnswerRouter.h"
#include "Language/Diagnostics/DiagnosticsModel.h"

#include "Core/Text/DocumentSet.h"

#include "FileFixtures.h"
#include "TempDirectory.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {
	class RecordingServer final : public DocumentServerPort
	{
	public:
		struct Call
		{
			std::string What;
			std::filesystem::path Path;
			std::string Text;
		};

		std::vector<Call> Calls;

		std::size_t CountOf(const std::string& what) const
		{
			std::size_t count = 0;

			for (const Call& call : Calls)
			{
				count += call.What == what ? 1 : 0;
			}

			return count;
		}

		void EnsureStarted(const std::filesystem::path& path, LanguageId) override { Calls.push_back({"start", path, {}}); }
		void OpenDocument(const std::filesystem::path& path, LanguageId, std::string text) override { Calls.push_back({"open", path, std::move(text)}); }
		void ChangeDocument(const std::filesystem::path& path, LanguageId, std::string text) override { Calls.push_back({"change", path, std::move(text)}); }
		void SaveDocument(const std::filesystem::path& path, LanguageId) override { Calls.push_back({"save", path, {}}); }
		void CloseDocument(const std::filesystem::path& path, LanguageId) override { Calls.push_back({"close", path, {}}); }
		ServerStatus StatusFor(const std::filesystem::path&, LanguageId) override { return {}; }
		bool HasChosenServer(LanguageId) override { return true; }
		bool OffersSwitchSourceHeader(LanguageId) override { return false; }
		LanguageServices::Ticket AskDefinition(const std::filesystem::path&, LanguageId, const FilePosition&) override { return {}; }
		LanguageServices::Ticket AskReferences(const std::filesystem::path&, LanguageId, const FilePosition&) override { return {}; }
		LanguageServices::Ticket AskHover(const std::filesystem::path&, LanguageId, const FilePosition&) override { return {}; }
		LanguageServices::Ticket AskSwitchSourceHeader(const std::filesystem::path&, LanguageId) override { return {}; }
		LanguageServices::Ticket AskSymbols(const std::filesystem::path& path, LanguageId) override
		{
			Calls.push_back({"symbols", path, {}});
			LastSymbols = NextTicket();
			return LastSymbols;
		}
		LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path&, LanguageId) override { return NextTicket(); }
		void Cancel(LanguageServices::Ticket) override {}
		void TakeAnswers(std::vector<LanguageServices::Answer>& answers) override
		{
			answers.insert(answers.end(), Queued.begin(), Queued.end());
			Queued.clear();
		}
		void TakeRecolour(std::vector<std::filesystem::path>&) override {}

		void Reply(LanguageServices::Ticket ticket)
		{
			LanguageServices::Answer answer;
			answer.For = ticket;
			Queued.push_back(std::move(answer));
		}

		LanguageServices::Ticket LastSymbols{};
		std::vector<LanguageServices::Answer> Queued;

	private:
		LanguageServices::Ticket NextTicket() { return static_cast<LanguageServices::Ticket>(++m_Next); }

		std::uint64_t m_Next = 0;
	};

	class SymbolsTarget final : public LspAnswerTarget
	{
	public:
		void ApplyDefinition(const DefinitionQuestion&, const LanguageServices::Answer&) override {}
		void ApplyReferences(const std::string&, const LanguageServices::Answer&) override {}
		void ApplySwitchSourceHeader(const std::filesystem::path&, const LanguageServices::Answer&) override {}
		void ApplyHover(const HoverQuestion&, const LanguageServices::Answer&) override {}
		void ApplyPreviewDefinition(const HoverQuestion&, const LanguageServices::Answer&) override {}
		void ApplySemanticTokens(const std::filesystem::path&, const LanguageServices::Answer&) override {}
		bool ApplySymbols(const std::filesystem::path&, const LanguageServices::Answer&) override { return true; }
		void ApplyCount(const std::filesystem::path&, std::size_t, std::size_t) override {}
		bool Offer(const LanguageServices::Answer&) override { return false; }
	};

	struct Harness
	{
		RecordingServer Server;
		LspAnswerRouter Router{Server, [] { return 0.0; }};
		DiagnosticsModel Diagnostics;
		DocumentStates::Clock::time_point Now{};
		DocumentStates States{Server, Router, Diagnostics, [this] { return Now; }};
		Core::DocumentSet Documents;
	};
}

TEST_CASE("A document opens with its server once however often it is shown")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	const Core::TextDocument& document = *harness.Documents.At(0).Document;

	CHECK(harness.States.OpenWithServer(path, document));
	CHECK_FALSE(harness.States.OpenWithServer(path, document));

	CHECK(harness.Server.CountOf("open") == 1);
	CHECK(harness.States.IsOpenWithServer(path));
}

TEST_CASE("An edit no tab drew still reaches the server")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	Core::TextDocument& document = *harness.Documents.At(0).Document;
	harness.States.OpenWithServer(path, document);

	document.GetBuffer().Insert(0, "// x\n");
	const std::vector<std::filesystem::path> sent = harness.States.SendChanges(harness.Documents.GetStore());

	REQUIRE(sent.size() == 1);
	REQUIRE(harness.Server.CountOf("change") == 1);
	CHECK(harness.Server.Calls.back().Text == "// x\nint a;\n");

	CHECK(harness.States.SendChanges(harness.Documents.GetStore()).empty());
	CHECK(harness.Server.CountOf("change") == 1);
}

TEST_CASE("A document never opened with its server sends no change")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);

	harness.Documents.At(0).Document->GetBuffer().Insert(0, "x");
	harness.States.SendChanges(harness.Documents.GetStore());

	CHECK(harness.Server.CountOf("change") == 0);
}

TEST_CASE("A document that left the store is closed with its server and forgotten")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	harness.States.OpenWithServer(path, *harness.Documents.At(0).Document);

	harness.Documents.Close(0);
	harness.States.Prune(harness.Documents.GetStore());

	CHECK(harness.Server.CountOf("close") == 1);
	CHECK(harness.States.Find(path) == nullptr);
	CHECK_FALSE(harness.States.IsOpenWithServer(path));
}

TEST_CASE("A renamed document is closed under its old path and opened under its new one with its unsaved text")
{
	TempDirectory directory;
	const std::filesystem::path from = directory.GetPath() / "old";
	const std::filesystem::path to = directory.GetPath() / "new";
	std::filesystem::create_directories(from);
	WriteFileBytes(from / "a.cpp", "int a;\n");
	Harness harness;
	REQUIRE(harness.Documents.Open(from / "a.cpp", Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	Core::TextDocument& document = *harness.Documents.At(0).Document;
	harness.States.OpenWithServer(from / "a.cpp", document);
	document.GetBuffer().Insert(0, "// unsaved\n");

	harness.Documents.RewritePaths(from, to);
	harness.States.Prune(harness.Documents.GetStore());
	harness.States.OpenWithServer(to / "a.cpp", document);

	CHECK(harness.Server.CountOf("close") == 1);
	REQUIRE(harness.Server.CountOf("open") == 2);
	CHECK(harness.Server.Calls.back().Path == to / "a.cpp");
	CHECK(harness.Server.Calls.back().Text == "// unsaved\nint a;\n");
}

TEST_CASE("A document opened with its server is not sent again by the sweep")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	harness.States.OpenWithServer(path, *harness.Documents.At(0).Document);

	CHECK(harness.States.SendChanges(harness.Documents.GetStore()).empty());
	CHECK(harness.Server.CountOf("change") == 0);
}

TEST_CASE("A change sent as it happened is not sent again by the sweep")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	Core::TextDocument& document = *harness.Documents.At(0).Document;
	harness.States.OpenWithServer(path, document);

	document.GetBuffer().Insert(0, "// x\n");
	CHECK(harness.States.SendChangeIfMoved(path, document));
	REQUIRE(harness.Server.CountOf("change") == 1);

	harness.States.SendChanges(harness.Documents.GetStore());
	CHECK(harness.Server.CountOf("change") == 1);
}

TEST_CASE("Saving a document asks for its symbols again")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	harness.States.OpenWithServer(path, *harness.Documents.At(0).Document);
	SymbolsTarget target;

	harness.Router.AskSymbolsIfDue(path);
	harness.Server.Reply(harness.Server.LastSymbols);
	harness.Router.Collect(target);
	harness.Router.AskSymbolsIfDue(path);
	REQUIRE(harness.Server.CountOf("symbols") == 1);

	harness.States.NoteSaved(path);
	harness.Router.AskSymbolsIfDue(path);

	CHECK(harness.Server.CountOf("save") == 1);
	CHECK(harness.Server.CountOf("symbols") == 2);
}

namespace {
	std::string TextLargerThanTheQuietThreshold()
	{
		std::string text;
		const std::string line = "int value = 0; // padding to make the document large\n";

		while (text.size() <= DocumentStates::kLargeDocumentBytes)
		{
			text += line;
		}

		return text;
	}
}

TEST_CASE("A large document is sent to the server only once it has stopped changing")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.cpp";
	WriteFileBytes(path, TextLargerThanTheQuietThreshold());
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	Core::TextDocument& document = *harness.Documents.At(0).Document;
	harness.States.OpenWithServer(path, document);

	document.GetBuffer().Insert(0, "a");
	CHECK(harness.States.SendChanges(harness.Documents.GetStore()).empty());

	harness.Now += std::chrono::milliseconds(100);
	document.GetBuffer().Insert(0, "b");
	harness.Now += std::chrono::milliseconds(100);
	CHECK(harness.States.SendChanges(harness.Documents.GetStore()).empty());
	CHECK(harness.Server.CountOf("change") == 0);

	harness.Now += DocumentStates::kLargeDocumentQuiet;

	REQUIRE(harness.States.SendChanges(harness.Documents.GetStore()).size() == 1);
	REQUIRE(harness.Server.CountOf("change") == 1);
	CHECK(harness.Server.Calls.back().Text.starts_with("ba"));
}

TEST_CASE("A large document is sent at once when the caller cannot wait")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "big.cpp";
	WriteFileBytes(path, TextLargerThanTheQuietThreshold());
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	Core::TextDocument& document = *harness.Documents.At(0).Document;
	harness.States.OpenWithServer(path, document);

	document.GetBuffer().Insert(0, "a");

	CHECK(harness.States.SendChangeIfMoved(path, document, SendTiming::Now));
	CHECK(harness.Server.CountOf("change") == 1);
}

TEST_CASE("A small document is sent at once however quickly it changes")
{
	TempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "a.cpp";
	WriteFileBytes(path, "int a;\n");
	Harness harness;
	REQUIRE(harness.Documents.Open(path, Core::TabKind::Permanent) == Core::OpenOutcome::Opened);
	Core::TextDocument& document = *harness.Documents.At(0).Document;
	harness.States.OpenWithServer(path, document);

	document.GetBuffer().Insert(0, "a");
	CHECK(harness.States.SendChanges(harness.Documents.GetStore()).size() == 1);

	document.GetBuffer().Insert(0, "b");
	CHECK(harness.States.SendChanges(harness.Documents.GetStore()).size() == 1);
	CHECK(harness.Server.CountOf("change") == 2);
}
