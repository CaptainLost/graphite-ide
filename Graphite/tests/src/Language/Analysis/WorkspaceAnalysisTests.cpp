#include <doctest/doctest.h>

#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Registry/LanguageRegistry.h"

#include "Language/Analysis/AnalysisStore.h"

#include "Editor/Overlays/AnalysisCache.h"

#include "AnalysisFixtures.h"
#include "TempDirectory.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace Graphite;

namespace {

	constexpr std::size_t kCacheCapacityBytes = 1024 * 1024;
	constexpr bool kForegroundQuiet = false;
	constexpr bool kForegroundBusy = true;
	constexpr bool kDisabled = false;
	constexpr bool kWalking = true;
	constexpr bool kNotWalking = false;
	constexpr int kFakeFileStampSeconds = 100;
	constexpr std::size_t kSemanticTokenLength = 3;

	class FakePort final : public WorkspaceAnalysisPort
	{
	public:
		struct Call
		{
			std::string What;
			std::filesystem::path Path;
		};

		void Open(const std::filesystem::path& path, LanguageId, std::string) override
		{
			Calls.push_back({"open", path});
		}

		LanguageServices::Ticket AskSemanticTokens(const std::filesystem::path& path,
												   LanguageId) override
		{
			Calls.push_back({"colours", path});

			return static_cast<LanguageServices::Ticket>(++Tickets);
		}

		LanguageServices::Ticket AskSymbols(const std::filesystem::path& path, LanguageId) override
		{
			Calls.push_back({"symbols", path});

			return static_cast<LanguageServices::Ticket>(++Tickets);
		}

		ServerReadiness ReadinessOf(const std::filesystem::path&, LanguageId language) override
		{
			const std::map<LanguageId, ServerReadiness>::const_iterator found =
				ReadinessByLanguage.find(language);

			if (found == ReadinessByLanguage.end())
			{
				return ServerReadiness::Ready;
			}

			const auto& [foundLanguage, readiness] = *found;

			return readiness;
		}

		std::map<LanguageId, ServerReadiness> ReadinessByLanguage;

		bool HasChosenServer(Graphite::LanguageId language) override
		{
			const bool hasServers = Graphite::LanguageHasServers(language);
			const bool isUnchosen =
				std::find(Unchosen.begin(), Unchosen.end(), language) != Unchosen.end();

			return hasServers && !isUnchosen;
		}

		std::vector<Graphite::LanguageId> Unchosen;

		std::string ServerNameOf(LanguageId) override { return "clangd"; }

		void Close(const std::filesystem::path& path, LanguageId) override
		{
			Calls.push_back({"close", path});
		}

		std::size_t CountOf(const std::string& what) const
		{
			std::size_t count = 0;

			for (const Call& call : Calls)
			{
				if (call.What == what)
				{
					++count;
				}
			}

			return count;
		}

		LanguageServices::Ticket TicketOf(const std::string& what,
										  const std::filesystem::path& path) const
		{
			std::uint64_t ticket = 0;

			for (const Call& call : Calls)
			{
				if (call.What == "colours" || call.What == "symbols")
				{
					++ticket;

					if (call.What == what && call.Path == path)
					{
						return static_cast<LanguageServices::Ticket>(ticket);
					}
				}
			}

			return static_cast<LanguageServices::Ticket>(0);
		}

		std::vector<Call> Calls;
		std::uint64_t Tickets = 0;
	};

	std::filesystem::file_time_type OnDisk()
	{
		return FileTimeAt(kFakeFileStampSeconds);
	}

	class FakeTexts final : public FileTextSource
	{
	public:
		void Request(const std::filesystem::path& path) override
		{
			Requested.push_back(path);

			FileText text;
			text.Path = path;
			text.Text = "int x;\n";
			text.ModifiedAt = OnDisk();
			text.Readable = Readable;

			Pending.push_back(std::move(text));
		}

		void Take(std::vector<FileText>& out) override
		{
			for (FileText& text : Pending)
			{
				out.push_back(std::move(text));
			}

			Pending.clear();
		}

		bool Readable = true;
		std::vector<FileText> Pending;
		std::vector<std::filesystem::path> Requested;
	};

	LanguageServices::Answer Colours(LanguageServices::Ticket ticket)
	{
		LanguageServices::Answer answer;
		answer.For = ticket;

		const std::size_t line = 0;
		const Utf16Column column(0);

		answer.SemanticTokens.push_back(
			SemanticToken{line, column, kSemanticTokenLength, SemanticKind::Type});

		return answer;
	}

	LanguageServices::Answer Symbols(LanguageServices::Ticket ticket)
	{
		LanguageServices::Answer answer;
		answer.For = ticket;

		Declaration declaration;
		declaration.Name = "x";
		declaration.Kind = DeclarationKind::Other;

		answer.Declarations.push_back(std::move(declaration));

		return answer;
	}

	LanguageServices::Answer Failed(LanguageServices::Ticket ticket)
	{
		LanguageServices::Answer answer;
		answer.For = ticket;
		answer.Failed = true;

		return answer;
	}

	bool NobodyElse(const std::filesystem::path&)
	{
		return false;
	}

	bool SomebodyElse(const std::filesystem::path&)
	{
		return true;
	}

	bool PreviewPaneHoldsOnlyA(const std::filesystem::path& path)
	{
		return path == std::filesystem::path{"a.cpp"};
	}
}

TEST_CASE("One file is opened at a time, however many are queued")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 1);
	CHECK(port.Calls.front().Path == std::filesystem::path{"a.cpp"});
}

TEST_CASE("A finished file is closed, cached, and the next one starts")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("close") == 1);

	const std::optional<CachedAnalysis> kept = cache.Take("a.cpp", OnDisk());

	REQUIRE(kept.has_value());
	CHECK_FALSE(kept->Semantic.IsEmpty());
	CHECK(kept->Declarations.size() == 1);

	analysis.Tick(4.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 2);
	CHECK(port.Calls.back().Path == std::filesystem::path{"b.cpp"});
}

TEST_CASE("Nothing new is launched while the open document is waiting on the server")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundBusy, NobodyElse);
	analysis.Tick(2.0, kForegroundBusy, NobodyElse);

	CHECK(port.Calls.empty());

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(4.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("A briefly busy document still holds everything back")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	for (double now = 1.0; now <= 10.0; now += 1.0)
	{
		analysis.Tick(now, kForegroundBusy, NobodyElse);
	}

	CHECK(port.Calls.empty());
}

TEST_CASE("A document busy for longer than anything real is stopped being waited for")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundBusy, NobodyElse);

	CHECK(port.Calls.empty());

	analysis.Tick(1.0 + 11.0, kForegroundBusy, NobodyElse);
	analysis.Tick(1.0 + 12.0, kForegroundBusy, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("Busy frames broken by a quiet one do not add up towards the cap")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Tick(1.0, kForegroundBusy, NobodyElse);
	analysis.Tick(10.0, kForegroundBusy, NobodyElse);

	analysis.Tick(11.0, kForegroundQuiet, NobodyElse);

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(12.0, kForegroundBusy, NobodyElse);
	analysis.Tick(20.0, kForegroundBusy, NobodyElse);

	CHECK(port.Calls.empty());
}

TEST_CASE("An analysis already outstanding is finished rather than abandoned when the editor gets "
		  "busy")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	analysis.Tick(3.0, kForegroundBusy, NobodyElse);

	CHECK(port.CountOf("close") == 0);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(4.0, kForegroundBusy, NobodyElse);

	CHECK(port.CountOf("close") == 1);
	CHECK(cache.Take("a.cpp", OnDisk()).has_value());
}

TEST_CASE("A file that somebody else already holds is never opened from here")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, SomebodyElse);
	analysis.Tick(2.0, kForegroundQuiet, SomebodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A file somebody else holds is left alone, whether a tab or other holder")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, PreviewPaneHoldsOnlyA);
	analysis.Tick(2.0, kForegroundQuiet, PreviewPaneHoldsOnlyA);

	CHECK(port.CountOf("open") == 1);
	CHECK(port.Calls.front().Path == std::filesystem::path{"b.cpp"});

	for (const FakePort::Call& call : port.Calls)
	{
		CHECK(call.Path != std::filesystem::path{"a.cpp"});
	}
}

TEST_CASE("A file taken over while its text is being read is never opened")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);

	analysis.Tick(2.0, kForegroundQuiet, SomebodyElse);

	CHECK(port.Calls.empty());

	CHECK(analysis.Progress().Done == 1);

	analysis.Tick(3.0, kForegroundQuiet, SomebodyElse);

	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A file that acquires a holder while it is outstanding is dropped and nothing is cached")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	analysis.Apply(Colours(port.TicketOf("colours", "a.cpp")));
	analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp")));

	analysis.Tick(3.0, kForegroundQuiet, SomebodyElse);

	CHECK_FALSE(cache.Take("a.cpp", OnDisk()).has_value());

	CHECK(port.CountOf("close") == 0);

	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("An analysis that times out after somebody else takes it over closes nothing")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	analysis.Tick(2.0 + 200.0, kForegroundQuiet, SomebodyElse);

	CHECK(port.CountOf("close") == 0);
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("An answer nobody here asked for is left for somebody else")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	CHECK_FALSE(analysis.Apply(Colours(static_cast<LanguageServices::Ticket>(9999))));

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK_FALSE(analysis.Apply(Colours(static_cast<LanguageServices::Ticket>(9999))));
	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
}

TEST_CASE("A failed request finishes the file without caching it, and the queue moves on")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(analysis.Apply(Failed(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Failed(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK_FALSE(cache.Take("a.cpp", OnDisk()).has_value());

	analysis.Tick(4.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 2);
	CHECK(port.Calls.back().Path == std::filesystem::path{"b.cpp"});
}

TEST_CASE("A file that cannot be read is dropped without being mentioned to a server")
{
	FakePort port;
	FakeTexts texts;
	texts.Readable = false;

	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.png"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A file in a language no server serves is dropped without being mentioned to one")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"notes.txt"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("A file whose language has no chosen server is counted done without being opened")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	port.Unchosen.push_back(LanguageId::Cpp);
	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(texts.Requested.empty());
	CHECK(analysis.Progress().Done == analysis.Progress().Total);
}

TEST_CASE("An analysis that is never answered is given up on rather than blocking the queue "
		  "forever")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	analysis.Tick(2.0 + 200.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("close") == 1);

	analysis.Tick(2.0 + 201.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 2);
	CHECK(port.Calls.back().Path == std::filesystem::path{"b.cpp"});
}

TEST_CASE("An analysis answered inside the timeout is not given up on")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	analysis.Tick(2.0 + 60.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("close") == 0);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(2.0 + 61.0, kForegroundQuiet, NobodyElse);

	CHECK(cache.Take("a.cpp", OnDisk()).has_value());
}

TEST_CASE("Disabled, it queues nothing and reports nothing")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetEnabled(kDisabled);
	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Total == 0);
}

TEST_CASE("Progress counts what is done against what was queued")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetWalking(kWalking);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	CHECK(analysis.Progress().Walking);
	CHECK(analysis.Progress().Total == 3);
	CHECK(analysis.Progress().Done == 0);

	analysis.SetWalking(kNotWalking);
	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);
	analysis.Apply(Colours(port.TicketOf("colours", "a.cpp")));
	analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp")));
	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK_FALSE(analysis.Progress().Walking);
	CHECK(analysis.Progress().Done == 1);
	CHECK(analysis.Progress().Total == 3);
}

TEST_CASE("Closing the folder drops the queue and closes what was open")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	analysis.Clear();

	CHECK(port.CountOf("close") == 1);
	CHECK(analysis.Progress().Total == 0);

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(4.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("Closing the folder while somebody else holds an outstanding file closes nothing")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	analysis.Tick(3.0, kForegroundQuiet, SomebodyElse);

	analysis.Clear();

	CHECK(port.CountOf("close") == 0);
	CHECK(analysis.Progress().Total == 0);
}

TEST_CASE("Destruction closes what was opened here, and nothing somebody else holds")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};

	SUBCASE("nothing holds the file, so it is closed")
	{
		{
			WorkspaceAnalysis analysis{port, texts, cache, nullptr};

			analysis.Enqueue({"a.cpp"});

			analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
			analysis.Tick(2.0, kForegroundQuiet, NobodyElse);
		}

		CHECK(port.CountOf("close") == 1);
	}

	SUBCASE("somebody else holds the file, so it is left open for them")
	{
		{
			WorkspaceAnalysis analysis{port, texts, cache, nullptr};

			analysis.Enqueue({"a.cpp"});

			analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
			analysis.Tick(2.0, kForegroundQuiet, NobodyElse);
			analysis.Tick(3.0, kForegroundQuiet, SomebodyElse);
		}

		CHECK(port.CountOf("close") == 0);
	}
}

TEST_CASE("Up to the limit are outstanding at once, and no more")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(3);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp", "d.cpp", "e.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 3);

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 3);
}

TEST_CASE("A limit of one behaves exactly as it did before there was a limit")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(1);
	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("A limit of zero is refused rather than stopping the pass")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(0);
	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("One path is never outstanding twice, however it reaches the queue")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(4);

	analysis.Enqueue({"a.cpp", "a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 1);

	CHECK(analysis.Progress().Total == 2);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("close") == 1);
	CHECK(analysis.Progress().Done == 2);
}

TEST_CASE("Finishing one of several starts the next, and closes only the one that finished")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(2);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	REQUIRE(port.CountOf("open") == 2);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "a.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "a.cpp"))));

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("close") == 1);
	CHECK(port.Calls.back().Path == std::filesystem::path{"a.cpp"});
	CHECK(analysis.Progress().Done == 1);

	analysis.Tick(4.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 3);
}

TEST_CASE("The yield holds back every slot, not just the next one")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(3);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	analysis.Tick(1.0, kForegroundBusy, NobodyElse);
	analysis.Tick(2.0, kForegroundBusy, NobodyElse);

	CHECK(port.Calls.empty());
}

TEST_CASE("Progress counts each file exactly once with several outstanding")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(3);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	for (const char* fileName : {"a.cpp", "b.cpp", "c.cpp"})
	{
		analysis.Apply(Colours(port.TicketOf("colours", fileName)));
		analysis.Apply(Symbols(port.TicketOf("symbols", fileName)));
	}

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(analysis.Progress().Done == 3);
	CHECK(analysis.Progress().Total == 3);
}

TEST_CASE("Lowering the limit starts nothing new and lets the outstanding ones finish")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(3);
	analysis.Enqueue({"a.cpp", "b.cpp", "c.cpp", "d.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	REQUIRE(port.CountOf("open") == 3);

	analysis.SetConcurrency(1);
	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 3);
	CHECK(port.CountOf("close") == 0);
}

TEST_CASE("One timing out does not disturb the others")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(2);
	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	REQUIRE(port.CountOf("open") == 2);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", "b.cpp"))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", "b.cpp"))));

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("close") == 1);

	analysis.Tick(2.0 + 200.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("close") == 2);
	CHECK(analysis.Progress().Done == 2);
}

TEST_CASE("A file the store already has is skipped rather than asked about again")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");

	AnalysisStore store;
	store.SetRoot(temp.GetPath());
	WaitForStoreReady(store);

	CachedAnalysis kept;
	Declaration declaration;
	declaration.Name = "Draw";
	kept.Declarations.push_back(std::move(declaration));
	kept.ModifiedAt = std::filesystem::last_write_time(source);

	store.Write(source, kept);

	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, &store};

	analysis.Enqueue({source});
	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 1);
}

TEST_CASE("Nothing is started while the store is still reading its index")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");

	AnalysisStore store;
	store.SetRoot(temp.GetPath());

	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, &store};

	analysis.Enqueue({source});

	REQUIRE_FALSE(store.IsReady());

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.Calls.empty());

	CHECK(analysis.Progress().Done == 0);
	CHECK(analysis.Progress().Total == 1);
	CHECK(analysis.Progress().Pause == AnalysisPause::WhileSavedAnalysisLoads);

	WaitForStoreReady(store);

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(4.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 1);
	CHECK(analysis.Progress().Pause == AnalysisPause::NotPaused);
}

TEST_CASE("A language held back while another is being analysed does not make the pass read as paused")
{
	FakePort port;
	port.ReadinessByLanguage[LanguageId::Cpp] = ServerReadiness::ComingUp;

	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};
	analysis.SetConcurrency(2);

	analysis.Enqueue({"a.cpp", "Program.cs"});
	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);

	REQUIRE(texts.Requested == std::vector<std::filesystem::path>{"Program.cs"});
	CHECK(analysis.Progress().Pause == AnalysisPause::NotPaused);

	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	REQUIRE(port.CountOf("open") == 1);
	CHECK(analysis.Progress().Pause == AnalysisPause::NotPaused);
}

TEST_CASE("Analysis held back by a server coming up is paused until that server is ready, and names it")
{
	FakePort port;
	port.ReadinessByLanguage[LanguageId::Cpp] = ServerReadiness::ComingUp;

	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.Enqueue({"a.cpp"});
	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);

	CHECK(analysis.Progress().Pause == AnalysisPause::UntilServerIsReady);
	CHECK(analysis.Progress().PausedForServer == "clangd");
	CHECK(port.Calls.empty());

	port.ReadinessByLanguage[LanguageId::Cpp] = ServerReadiness::Ready;
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(analysis.Progress().Pause == AnalysisPause::NotPaused);
	CHECK(analysis.Progress().PausedForServer.empty());
}

TEST_CASE("A completed analysis reaches the store as well as the memory cache")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");

	AnalysisStore store;
	store.SetRoot(temp.GetPath());
	WaitForStoreReady(store);

	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, &store};

	analysis.Enqueue({source});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", source))));
	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", source))));

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(cache.Take(source, OnDisk()).has_value());

	CHECK(store.Has(source));

	WaitForStoreWrites(store);

	CHECK(store.Read(source, OnDisk()).has_value());
}

TEST_CASE("A holder that comes and goes between the two answers leaves nothing in either tier")
{
	TempDirectory temp;
	const std::filesystem::path source = temp.MakeFile("a.cpp", "int x;");

	AnalysisStore store;
	store.SetRoot(temp.GetPath());
	WaitForStoreReady(store);

	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, &store};

	analysis.Enqueue({source});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	analysis.Tick(3.0, kForegroundQuiet, SomebodyElse);

	CHECK(analysis.Apply(Symbols(port.TicketOf("symbols", source))));

	analysis.Tick(4.0, kForegroundQuiet, NobodyElse);

	CHECK(analysis.Apply(Colours(port.TicketOf("colours", source))));

	analysis.Tick(5.0, kForegroundQuiet, NobodyElse);

	CHECK_FALSE(cache.Take(source, OnDisk()).has_value());
	CHECK_FALSE(store.Has(source));

	WaitForStoreWrites(store);

	CHECK_FALSE(store.Read(source, OnDisk()).has_value());

	CHECK(analysis.Progress().Done == 1);
	CHECK(port.CountOf("close") == 1);
}

TEST_CASE("Nothing is asked of a server that is still starting up")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	port.ReadinessByLanguage[LanguageId::Cpp] = ServerReadiness::ComingUp;
	analysis.Enqueue({"a.cpp", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 0);
	CHECK(analysis.Progress().Total == 2);
}

TEST_CASE("The queue proceeds once the server can answer")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	port.ReadinessByLanguage[LanguageId::Cpp] = ServerReadiness::ComingUp;
	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);

	REQUIRE(port.Calls.empty());

	port.ReadinessByLanguage[LanguageId::Cpp] = ServerReadiness::Ready;

	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 1);
}

TEST_CASE("A server coming up holds its language for as long as it takes")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	port.ReadinessByLanguage[LanguageId::Cpp] = ServerReadiness::ComingUp;
	analysis.Enqueue({"a.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(1.0 + 1000.0, kForegroundQuiet, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 0);
}

TEST_CASE("A server loading its projects holds its own language and no other")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	analysis.SetConcurrency(2);
	port.ReadinessByLanguage[LanguageId::CSharp] = ServerReadiness::ComingUp;
	analysis.Enqueue({"a.cs", "b.cpp"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(2.0, kForegroundQuiet, NobodyElse);

	REQUIRE(port.CountOf("open") == 1);
	CHECK(port.Calls.front().Path == std::filesystem::path{"b.cpp"});

	port.ReadinessByLanguage[LanguageId::CSharp] = ServerReadiness::Ready;

	analysis.Tick(3.0, kForegroundQuiet, NobodyElse);
	analysis.Tick(4.0, kForegroundQuiet, NobodyElse);

	CHECK(port.CountOf("open") == 2);
}

TEST_CASE("A file whose server cannot run is counted done at once")
{
	FakePort port;
	FakeTexts texts;
	AnalysisCache cache{kCacheCapacityBytes};
	WorkspaceAnalysis analysis{port, texts, cache, nullptr};

	port.ReadinessByLanguage[LanguageId::CSharp] = ServerReadiness::CannotRun;
	analysis.Enqueue({"a.cs", "b.cs", "c.cs"});

	analysis.Tick(1.0, kForegroundQuiet, NobodyElse);

	CHECK(port.Calls.empty());
	CHECK(analysis.Progress().Done == 3);
	CHECK(analysis.Progress().Total == 3);
}

TEST_CASE("A server reads as coming up only while it starts or loads its projects")
{
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::Starting}) == ServerReadiness::ComingUp);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::Indexing, .Loading = "Demo.slnx"}) == ServerReadiness::ComingUp);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::Indexing}) == ServerReadiness::Ready);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::Ready}) == ServerReadiness::Ready);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::ProjectIncomplete}) == ServerReadiness::Ready);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::NotConfigured}) == ServerReadiness::Ready);
}

TEST_CASE("A server that cannot run reads so and a failed one only once its restarts are spent")
{
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::NotFound}) == ServerReadiness::CannotRun);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::NotInstalled}) == ServerReadiness::CannotRun);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::MissingRuntime}) == ServerReadiness::CannotRun);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::Restricted}) == ServerReadiness::CannotRun);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::Off}) == ServerReadiness::CannotRun);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::Failed, .Restarts = 0}) == ServerReadiness::ComingUp);
	CHECK(ReadinessFrom(ServerStatus{.State = ServerState::Failed, .Restarts = kMaximumRestarts}) == ServerReadiness::CannotRun);
}
