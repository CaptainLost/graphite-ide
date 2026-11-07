#include "Git/Tags/TagAcceptance.h"

#include "Git/Content/ContentModel.h"
#include "Git/Content/ContentPort.h"
#include "Git/History/CommitRefs.h"
#include "Git/Lines/BranchLines.h"
#include "Git/Lines/LineReader.h"
#include "Git/Lines/LineStore.h"
#include "Git/Sidebar/SidebarRows.h"
#include "Git/Tags/FirstTag.h"
#include "Git/Tags/TagFolders.h"

#include "Core/FileSystem/FileSystem.h"
#include "Core/Git/Lines/LineQuestions.h"
#include "Core/Git/Repository.h"
#include "Core/Git/Tag.h"

#include "TempDirectory.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Graphite;

namespace {
	using Clock = std::chrono::steady_clock;

	constexpr std::size_t kSampledCommits = 200;
	constexpr std::size_t kDifferencesNamed = 20;

	double MillisecondsSince(Clock::time_point start)
	{
		return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	}

	struct Timing
	{
		double Best = 0.0;
		double Median = 0.0;
	};

	Timing Summarise(std::vector<double> runs)
	{
		REQUIRE_FALSE(runs.empty());
		std::ranges::sort(runs);
		return Timing{.Best = runs.front(), .Median = runs[runs.size() / 2]};
	}

	std::string Described(const Timing& timing)
	{
		return std::format("{:.2f} ms best, {:.2f} ms median", timing.Best, timing.Median);
	}

	// `times` runs of `run`, each timed alone.
	Timing Timed(int times, const std::function<void()>& run)
	{
		std::vector<double> runs;

		for (int time = 0; time < times; ++time)
		{
			const Clock::time_point start = Clock::now();
			run();
			runs.push_back(MillisecondsSince(start));
		}

		return Summarise(std::move(runs));
	}

	Core::Repository OpenRepository(const std::string& path)
	{
		Core::Result<Core::Repository> repository = Core::Repository::Open(Core::FromUtf8(path));
		INFO((repository.HasValue() ? std::string{} : repository.GetError().Message));
		REQUIRE(repository.HasValue());
		return std::move(repository).GetValue();
	}

	class PortAskedNothing final : public ContentPort
	{
	public:
		std::uint64_t RepositoryNow() override { return 1; }

		Core::Result<Core::ContentVerdict> CheckContent(std::uint64_t, const Core::Oid&, const Core::Oid&, Core::PatchIdCache&) override
		{
			return NotAsked();
		}

		Core::Result<Core::PatchTwinSearch> FindTwin(std::uint64_t, const Core::Oid&, const Core::Oid&, std::size_t, Core::PatchIdCache&) override
		{
			return NotAsked();
		}

		Core::Result<std::vector<std::string>> PathsChangedBetween(std::uint64_t, const Core::Oid&, const Core::Oid&) override
		{
			return NotAsked();
		}

		Core::Result<bool> TreesDifferAt(std::uint64_t, const Core::Oid&, const Core::Oid&, const std::vector<std::string>&) override
		{
			return NotAsked();
		}

	private:
		static Core::Error NotAsked() { return Core::Error{Core::ErrorCode::Cancelled, "not asked"}; }
	};

	void DropJob(std::function<void()>)
	{
	}

	// Every field of the two models that differs, named ref by ref; empty when the models are the same.
	std::vector<std::string> ModelDifferences(const LineRead& without, const LineRead& with)
	{
		std::vector<std::string> differences;

		if (without.Refs.size() != with.Refs.size())
		{
			differences.push_back(std::format("{} refs against {}", without.Refs.size(), with.Refs.size()));
			return differences;
		}

		for (std::size_t index = 0; index < without.Refs.size(); ++index)
		{
			if (without.Refs[index].FullName != with.Refs[index].FullName || without.Refs[index].Tip != with.Refs[index].Tip)
			{
				differences.push_back("ref " + without.Refs[index].FullName);
			}
		}

		const Core::LineModel& a = without.Model;
		const Core::LineModel& b = with.Model;

		if (a.MainLineGuessed != b.MainLineGuessed)
		{
			differences.push_back("MainLineGuessed");
		}

		if (a.Bases.size() != b.Bases.size())
		{
			differences.push_back(std::format("{} bases against {}", a.Bases.size(), b.Bases.size()));
		}

		for (std::size_t index = 0; index < std::min(a.Bases.size(), b.Bases.size()); ++index)
		{
			const Core::LineBase& x = a.Bases[index];
			const Core::LineBase& y = b.Bases[index];

			if (x.Ref != y.Ref || x.Reason != y.Reason || !(x.Qualities == y.Qualities) || x.Active != y.Active)
			{
				differences.push_back("base " + without.Refs[x.Ref].FullName);
			}
		}

		if (a.OldCopies.size() != b.OldCopies.size())
		{
			differences.push_back(std::format("{} old copies against {}", a.OldCopies.size(), b.OldCopies.size()));
		}

		for (std::size_t index = 0; index < std::min(a.OldCopies.size(), b.OldCopies.size()); ++index)
		{
			if (a.OldCopies[index].Ref != b.OldCopies[index].Ref || a.OldCopies[index].Of != b.OldCopies[index].Of)
			{
				differences.push_back("old copy " + without.Refs[a.OldCopies[index].Ref].FullName);
			}
		}

		if (a.Branches.size() != b.Branches.size())
		{
			differences.push_back(std::format("{} branch lines against {}", a.Branches.size(), b.Branches.size()));
		}

		for (std::size_t index = 0; index < std::min(a.Branches.size(), b.Branches.size()); ++index)
		{
			const Core::BranchLine& x = a.Branches[index];
			const Core::BranchLine& y = b.Branches[index];

			const bool same = x.Ref == y.Ref && x.Kind == y.Kind && x.ComparedWith == y.ComparedWith && x.ByAnswer == y.ByAnswer &&
							  x.AutomaticComparedWith == y.AutomaticComparedWith && x.AutomaticKind == y.AutomaticKind &&
							  x.State == y.State && x.Ahead == y.Ahead && x.SameRowAs == y.SameRowAs;

			if (!same)
			{
				differences.push_back("branch line " + without.Refs[x.Ref].FullName);
			}
		}

		if (!(without.Answers == with.Answers))
		{
			differences.push_back("answers");
		}

		return differences;
	}

	void ReportSameModel(const char* what, const LineRead& without, const LineRead& with)
	{
		const std::vector<std::string> differences = ModelDifferences(without, with);

		MESSAGE(std::string{what}, ": ", without.Refs.size(), " refs, ", without.Model.Bases.size(), " bases, ", without.Model.Branches.size(),
				" branch lines; ", differences.size(), " differences with the tags");

		for (std::size_t index = 0; index < std::min(differences.size(), kDifferencesNamed); ++index)
		{
			MESSAGE("  differs: ", differences[index]);
		}

		CHECK(differences.empty());
	}

	struct ReadOutcome
	{
		LineRead Read;
		double Milliseconds = 0.0;
	};

	ReadOutcome TimedRead(const Core::Repository& repository, const LineStorePaths& store, const LineReadInput& input)
	{
		const Clock::time_point start = Clock::now();
		Core::Result<LineRead> read = ReadLines(repository, store, input);
		const double milliseconds = MillisecondsSince(start);

		INFO((read.HasValue() ? std::string{} : read.GetError().Message));
		REQUIRE(read.HasValue());
		return ReadOutcome{.Read = std::move(read).GetValue(), .Milliseconds = milliseconds};
	}

	// The tags as listed, and what they hold: annotated, lightweight, and the folders the sidebar makes of them.
	void ReportTagCounts(const std::vector<Core::Tag>& tags)
	{
		std::size_t annotated = 0;
		std::map<std::string, std::size_t> folders;

		for (const Core::Tag& tag : tags)
		{
			annotated += tag.Annotated ? 1 : 0;

			if (const std::string_view folder = FolderOfTagName(tag.Name); !folder.empty())
			{
				++folders[std::string{folder}];
			}
		}

		MESSAGE("tags listed: ", tags.size(), " (", annotated, " annotated, ", tags.size() - annotated, " lightweight), ",
				folders.size(), " folders");

		for (const auto& [folder, count] : folders)
		{
			MESSAGE("  folder ", folder, "/: ", count);
		}
	}

	// The branch listing and the tag listing as the branch job runs them, one after the other: cold with a fresh handle and
	// a fresh cache each time, warm with the handle and the cache of the runs before.
	Core::Ref<const std::vector<Core::Tag>> TimeListings(const std::string& repositoryPath)
	{
		constexpr int kColdRuns = 3;
		constexpr int kWarmRuns = 5;

		std::vector<double> branchesCold;
		std::vector<double> tagsCold;

		for (int run = 0; run < kColdRuns; ++run)
		{
			const Core::Repository repository = OpenRepository(repositoryPath);
			Core::TagCache cache;

			const Clock::time_point branchStart = Clock::now();
			REQUIRE(repository.QueryBranches().HasValue());
			branchesCold.push_back(MillisecondsSince(branchStart));

			const Clock::time_point tagStart = Clock::now();
			REQUIRE(repository.QueryTags(cache).HasValue());
			tagsCold.push_back(MillisecondsSince(tagStart));
		}

		const Core::Repository repository = OpenRepository(repositoryPath);
		Core::TagCache cache;
		Core::Result<std::vector<Core::Tag>> first = repository.QueryTags(cache);
		REQUIRE(first.HasValue());

		std::vector<double> branchesWarm;
		std::vector<double> tagsWarm;

		for (int run = 0; run < kWarmRuns; ++run)
		{
			const Clock::time_point branchStart = Clock::now();
			REQUIRE(repository.QueryBranches().HasValue());
			branchesWarm.push_back(MillisecondsSince(branchStart));

			const Clock::time_point tagStart = Clock::now();
			const Core::Result<std::vector<Core::Tag>> again = repository.QueryTags(cache);
			tagsWarm.push_back(MillisecondsSince(tagStart));
			REQUIRE(again.HasValue());
			CHECK(again.GetValue() == first.GetValue());
		}

		const Timing coldBranches = Summarise(branchesCold);
		const Timing coldTags = Summarise(tagsCold);
		const Timing warmBranches = Summarise(branchesWarm);
		const Timing warmTags = Summarise(tagsWarm);

		MESSAGE("branch listing cold: ", Described(coldBranches), "; tag listing cold (fresh handle and cache): ", Described(coldTags),
				"; the tags' share of the job: ", 100.0 * coldTags.Median / (coldTags.Median + coldBranches.Median), " %");
		MESSAGE("branch listing warm: ", Described(warmBranches), "; tag listing warm (", cache.GetSize(), " tag objects cached): ",
				Described(warmTags), "; the tags' share of the job: ", 100.0 * warmTags.Median / (warmTags.Median + warmBranches.Median), " %");

		return Core::CreateRef<const std::vector<Core::Tag>>(std::move(first).GetValue());
	}

	// The commits whose first tag is compared with git's: kSampledCommits spread evenly over the graph.
	void WriteFirstTagSample(const LineRead& read, const std::string& outPath)
	{
		std::ofstream out{Core::FromUtf8(outPath), std::ios::binary | std::ios::trunc};
		REQUIRE(out.is_open());

		const std::size_t size = read.Graph->Size();
		std::set<Core::AncestryGraph::Index> sampled;

		for (std::size_t sample = 0; sample < kSampledCommits && size > 0; ++sample)
		{
			sampled.insert(static_cast<Core::AncestryGraph::Index>(sample * size / kSampledCommits));
		}

		std::size_t named = 0;

		for (const Core::AncestryGraph::Index index : sampled)
		{
			const Core::Oid& commit = read.Graph->IdOf(index);
			const Core::Tag* tag = FirstTagOf(read, commit);
			named += tag != nullptr ? 1 : 0;

			out << commit.ToHex() << '\t' << (tag != nullptr ? tag->FullName : std::string{}) << '\t'
				<< (tag != nullptr ? tag->When.time_since_epoch().count() : 0) << '\n';
		}

		MESSAGE("first tag of ", sampled.size(), " sampled commits written, ", named, " held by a tag");
	}

	// What follows a new read on the UI thread when only the tags changed: the content model's Follow, the sidebar's line
	// views and rows, the reviews' line views and the commit pills.
	void TimeTagsOnlyChangeOnTheUiThread(const Core::Ref<const LineRead>& before, const Core::Ref<const LineRead>& after)
	{
		constexpr int kRuns = 5;

		std::vector<double> follows;

		for (int run = 0; run < kRuns; ++run)
		{
			PortAskedNothing port;
			ContentModel model{port, DropJob};
			model.Follow(before);

			const Clock::time_point start = Clock::now();
			model.Follow(after);
			follows.push_back(MillisecondsSince(start));
		}

		const Timing follow = Summarise(follows);
		const Timing lines = Timed(kRuns, [&after] { const BranchLines built{*after}; });

		const std::map<std::string, bool> openByUser;
		const std::set<std::string> showingMerged;
		const SidebarInput input{.Read = *after,
								 .Stashes = nullptr,
								 .Authors = nullptr,
								 .Search = {},
								 .OpenByUser = openByUser,
								 .ShowingMerged = showingMerged,
								 .Tags = after->Tags.get()};
		const Timing rows = Timed(kRuns, [&input] { static_cast<void>(BuildSidebarRows(input)); });
		const Timing pills = Timed(kRuns, [&after] { static_cast<void>(BuildCommitRefs(*after->Branches, *after->Tags)); });

		MESSAGE("a tags-only change on the UI thread: content Follow ", Described(follow), "; line views (sidebar and reviews each) ",
				Described(lines), "; sidebar rows, Tags closed, ", Described(rows), "; commit pills ", Described(pills),
				"; sum of the medians ", follow.Median + 2.0 * lines.Median + rows.Median + pills.Median, " ms");
	}

	std::size_t RowsIn(const std::vector<SidebarRow>& rows, SidebarSection section)
	{
		std::size_t count = 0;

		for (const SidebarRow& row : rows)
		{
			count += row.Section == section ? 1 : 0;
		}

		return count;
	}

	void ReadAgain(const Core::Repository& repository, const LineStorePaths& store, const LineReadInput& input)
	{
		REQUIRE(ReadLines(repository, store, input).HasValue());
	}

	// The sidebar's rows built from the read with every tag: the section closed, opened, opened with every folder, and
	// narrowed by a search.
	void TimeSidebarRows(const LineRead& read)
	{
		constexpr int kRuns = 30;

		const std::set<std::string> showingMerged;
		std::map<std::string, bool> closed;
		std::map<std::string, bool> opened{{"tags", true}};
		std::map<std::string, bool> everyFolder{{"tags", true}};

		for (const Core::Tag& tag : *read.Tags)
		{
			if (const std::string_view folder = FolderOfTagName(tag.Name); !folder.empty())
			{
				everyFolder["tags/" + std::string{folder}] = true;
			}
		}

		struct Case
		{
			const char* Name;
			const std::map<std::string, bool>* Open;
			std::string_view Search;
		};

		const std::vector<Case> cases{{"Tags closed", &closed, {}},
									  {"Tags open", &opened, {}},
									  {"Tags and every folder open", &everyFolder, {}},
									  {"searching \"1\"", &opened, "1"},
									  {"searching \"canary 15\"", &opened, "canary 15"}};

		for (const Case& sidebarCase : cases)
		{
			const SidebarInput input{.Read = read,
									 .Stashes = nullptr,
									 .Authors = nullptr,
									 .Search = sidebarCase.Search,
									 .OpenByUser = *sidebarCase.Open,
									 .ShowingMerged = showingMerged,
									 .Tags = read.Tags.get()};

			std::vector<SidebarRow> rows;
			const Timing built = Timed(kRuns, [&input, &rows] { rows = BuildSidebarRows(input); });

			MESSAGE("sidebar rows, ", std::string{sidebarCase.Name}, ": ", rows.size(), " rows (", RowsIn(rows, SidebarSection::Tags), " in Tags) in ",
					Described(built));
		}
	}
}

void RunTagAcceptance(const std::string& repositoryPath, std::int64_t now, const std::string& sampleOutPath)
{
	const Core::Ref<const std::vector<Core::Tag>> tags = TimeListings(repositoryPath);
	ReportTagCounts(*tags);

	const Core::Repository repository = OpenRepository(repositoryPath);
	const Core::Result<std::vector<Core::Branch>> listed = repository.QueryBranches();
	REQUIRE(listed.HasValue());
	const Core::Ref<const std::vector<Core::Branch>> branches = Core::CreateRef<const std::vector<Core::Branch>>(listed.GetValue());

	// Two rounds of fresh stores, without the tags first, so the second round's two reads find the same file cache.
	std::optional<ReadOutcome> without;
	std::optional<ReadOutcome> with;
	std::optional<TempDirectory> withoutDirectory;
	std::optional<TempDirectory> withDirectory;
	std::vector<double> coldWithout;
	std::vector<double> coldWith;

	for (int round = 0; round < 2; ++round)
	{
		withoutDirectory.emplace();
		withDirectory.emplace();
		const LineStorePaths withoutStore = LineStoreFor(repository.CommonDirectory(), withoutDirectory->GetPath() / "store");
		const LineStorePaths withStore = LineStoreFor(repository.CommonDirectory(), withDirectory->GetPath() / "store");

		without = TimedRead(repository, withoutStore, LineReadInput{.Branches = branches, .Now = now});
		with = TimedRead(repository, withStore, LineReadInput{.Branches = branches, .Tags = tags, .Now = now});
		coldWithout.push_back(without->Milliseconds);
		coldWith.push_back(with->Milliseconds);
	}

	MESSAGE("cold read without tags: ", coldWithout[0], " ms then ", coldWithout[1], " ms; with them: ", coldWith[0], " ms then ",
			coldWith[1], " ms; the tags add ", coldWith[1] - coldWithout[1], " ms to the second round's");
	MESSAGE("graph: ", without->Read.Graph->Size(), " commits without tags, ", with->Read.Graph->Size(), " with them: the tags add ",
			with->Read.Graph->Size() - without->Read.Graph->Size(), " commits no branch reaches");

	ReportSameModel("cold reads", without->Read, with->Read);

	// The second opening: each store's graph.bin and answers read back, nothing in hand.
	const LineStorePaths withoutStore = LineStoreFor(repository.CommonDirectory(), withoutDirectory->GetPath() / "store");
	const LineStorePaths withStore = LineStoreFor(repository.CommonDirectory(), withDirectory->GetPath() / "store");
	const ReadOutcome reopenedWithout = TimedRead(repository, withoutStore, LineReadInput{.Branches = branches, .Now = now});
	const ReadOutcome reopenedWith = TimedRead(repository, withStore, LineReadInput{.Branches = branches, .Tags = tags, .Now = now});
	CHECK(reopenedWith.Read.CommitsRead == 0);

	MESSAGE("second opening's read from graph.bin: ", reopenedWithout.Milliseconds, " ms without tags, ", reopenedWith.Milliseconds,
			" ms with them: the tags add ", reopenedWith.Milliseconds - reopenedWithout.Milliseconds, " ms");

	ReportSameModel("reads from graph.bin", reopenedWithout.Read, reopenedWith.Read);

	const LineRead& read = reopenedWith.Read;
	const Timing pass = Timed(5, [&read] { static_cast<void>(FirstTagsIn(*read.Graph, *read.Tags)); });
	std::size_t held = 0;

	for (const std::uint32_t firstTag : read.FirstTag)
	{
		held += firstTag != Core::kNoTag ? 1 : 0;
	}

	MESSAGE("first-tag pass over ", read.Graph->Size(), " commits and ", read.Tags->size(), " tags: ", Described(pass), "; ", held,
			" commits held by a tag");

	// A tags-only change: the newest tag is added, the graph and generations in hand, as LinesModel asks it.
	REQUIRE_FALSE(tags->empty());
	const Core::Ref<const std::vector<Core::Tag>> fewer =
		Core::CreateRef<const std::vector<Core::Tag>>(std::next(tags->begin()), tags->end());
	const ReadOutcome before = TimedRead(
		repository, withStore,
		LineReadInput{.Branches = branches, .Tags = fewer, .Graph = read.Graph, .Now = now, .Generations = read.Generations});

	std::vector<double> tagsOnly;
	std::optional<ReadOutcome> after;

	for (int run = 0; run < 3; ++run)
	{
		after = TimedRead(repository, withStore,
						  LineReadInput{.Branches = branches, .Tags = tags, .Graph = before.Read.Graph, .Now = now,
										.Generations = before.Read.Generations});
		tagsOnly.push_back(after->Milliseconds);
	}

	const LineReadInput sameWithout{.Branches = branches, .Graph = reopenedWithout.Read.Graph, .Now = now,
									.Generations = reopenedWithout.Read.Generations};
	const Timing inHandWithout = Timed(3, [&] { ReadAgain(repository, withoutStore, sameWithout); });

	CHECK(after->Read.Graph == before.Read.Graph);
	MESSAGE("a tags-only change read again from the graph in hand: ", Described(Summarise(tagsOnly)), " on the worker (the same read ",
			"without tags: ", Described(inHandWithout), "), a new LineRead every follower follows again");
	ReportSameModel("the tags-only change's read", before.Read, after->Read);

	TimeTagsOnlyChangeOnTheUiThread(Core::CreateRef<const LineRead>(before.Read), Core::CreateRef<const LineRead>(after->Read));
	TimeSidebarRows(read);

	if (!sampleOutPath.empty())
	{
		WriteFirstTagSample(read, sampleOutPath);
	}
}
