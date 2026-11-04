#include <doctest/doctest.h>

#include "Git/Sidebar/SidebarText.h"

#include "Git/Lines/LineReadFixtures.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace Graphite;
using Graphite::Fixtures::BranchSpec;
using Graphite::Fixtures::LineReadBuilder;

namespace {

	SidebarRow BranchRow(std::size_t ref, SidebarSection section = SidebarSection::Yours)
	{
		SidebarRow row;
		row.Kind = SidebarRowKind::Branch;
		row.Section = section;
		row.Ref = ref;
		return row;
	}

	SidebarRow BaseRow(std::size_t ref, std::optional<std::size_t> localCopy = std::nullopt)
	{
		SidebarRow row;
		row.Kind = SidebarRowKind::Base;
		row.Section = SidebarSection::Bases;
		row.Ref = ref;
		row.LocalCopy = localCopy;
		return row;
	}

	Core::BranchLine& LineOfRef(LineRead& read, std::size_t ref)
	{
		const auto found = std::find_if(read.Model.Branches.begin(), read.Model.Branches.end(),
										[ref](const Core::BranchLine& line) { return line.Ref == ref; });
		REQUIRE(found != read.Model.Branches.end());
		return *found;
	}

	Core::LineBase& BaseOfRef(LineRead& read, std::size_t ref)
	{
		const auto found = std::find_if(read.Model.Bases.begin(), read.Model.Bases.end(),
										[ref](const Core::LineBase& base) { return base.Ref == ref; });
		REQUIRE(found != read.Model.Bases.end());
		return *found;
	}

	SidebarRowText Describe(const LineRead& read, const SidebarRow& row, const std::optional<std::string>& email = std::nullopt)
	{
		const BranchLines lines(read);
		const std::string common = MostCommonLineOf(read);
		const SidebarTextInput input{.Read = read, .Lines = lines, .UserEmail = email, .MostCommonLine = common, .Now = LineReadBuilder::Now()};
		return DescribeSidebarRow(row, input);
	}

	std::vector<std::string> Texts(const SidebarRowText& text)
	{
		std::vector<std::string> out;

		for (const SidebarSpan& span : text.Meta)
		{
			out.push_back(span.Text);
		}

		return out;
	}

	const SidebarSpan* SpanWith(const SidebarRowText& text, const std::string& words)
	{
		for (const SidebarSpan& span : text.Meta)
		{
			if (span.Text == words)
			{
				return &span;
			}
		}

		return nullptr;
	}

	using Strings = std::vector<std::string>;
}

TEST_CASE("Git window: a local branch without upstream reads local only")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t main = builder.AddRemoteBranch("origin", "main");
	builder.MakeBase(main);
	const std::size_t mine = builder.AddLocal("fix/typo");
	LineRead read = builder.Build();

	const SidebarRowText text = Describe(read, BranchRow(mine));

	CHECK(text.Head == "typo");
	CHECK(Texts(text) == Strings{"local only", "fix", "Ann Author", "4w ago"});
	CHECK(text.Meta[0].Tone == SidebarTone::Local);
	CHECK(text.Tooltip.find("Local only, on no remote") != std::string::npos);
	CHECK(text.Tooltip.starts_with("fix/typo\nAnn Author, 4w ago"));
}

TEST_CASE("Git window: a branch tracking a remote branch reads local plus the remote")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t remote = builder.AddRemoteBranch("origin", "feature");
	const std::size_t local = builder.AddLocal("feature");
	builder.SameRow(local, remote);
	LineRead read = builder.Build();

	const SidebarRowText text = Describe(read, BranchRow(local));

	CHECK(text.Meta[0].Text == "local + origin");
	CHECK(text.Meta[0].Tone == SidebarTone::Both);
}

TEST_CASE("Git window: a branch whose remote branch was deleted reads deleted on its remote")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t gone = builder.AddLocal("old", BranchSpec{.MissingUpstream = "origin/old"});
	LineRead read = builder.Build();

	const SidebarRowText text = Describe(read, BranchRow(gone));

	REQUIRE(text.Meta.size() >= 2);
	CHECK(text.Meta[0].Text == "local only");
	CHECK(text.Meta[1].Text == "deleted on origin");
	CHECK(text.Meta[1].Tone == SidebarTone::Gone);
	CHECK(text.Tooltip.find("Deleted on origin; only the local copy is left") != std::string::npos);
}

TEST_CASE("Git window: a row in a remote section says nothing of where it lives")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t remote = builder.AddRemoteBranch("origin", "team/feature");
	LineRead read = builder.Build();

	const SidebarRowText text = Describe(read, BranchRow(remote, SidebarSection::Remote));

	CHECK(text.Head == "feature");
	CHECK(Texts(text) == Strings{"team", "Ann Author", "4w ago"});
}

TEST_CASE("Git window: the user's own branch reads you")
{
	LineReadBuilder builder;
	const std::size_t mine = builder.AddLocal("mine", BranchSpec{.AuthorEmail = "Ann@Example.com "});
	LineRead read = builder.Build();

	const std::optional<std::string> email = "ann@example.com";
	const SidebarRowText own = Describe(read, BranchRow(mine), email);
	const SidebarRowText other = Describe(read, BranchRow(mine), std::optional<std::string>("bob@example.com"));

	CHECK(Texts(own) == Strings{"local only", "you", "4w ago"});
	CHECK(Texts(other) == Strings{"local only", "Ann Author", "4w ago"});
}

TEST_CASE("Git window: a checked out branch is marked as the head")
{
	LineReadBuilder builder;
	const std::size_t head = builder.AddLocal("main", BranchSpec{.Head = true});
	const std::size_t other = builder.AddLocal("other");
	LineRead read = builder.Build();

	CHECK(Describe(read, BranchRow(head)).IsHead);
	CHECK_FALSE(Describe(read, BranchRow(other)).IsHead);
}

TEST_CASE("Git window: a branch with no own commits and a merged branch say so")
{
	LineReadBuilder builder;
	const std::size_t empty = builder.AddLocal("empty", BranchSpec{.State = Core::BranchState::NoOwnCommits});
	const std::size_t merged = builder.AddLocal("done", BranchSpec{.State = Core::BranchState::MergedByHistory});
	LineRead read = builder.Build();

	const SidebarRowText emptyText = Describe(read, BranchRow(empty));
	const SidebarRowText mergedText = Describe(read, BranchRow(merged));

	REQUIRE(SpanWith(emptyText, "no own commits yet") != nullptr);
	CHECK(SpanWith(emptyText, "no own commits yet")->Tone == SidebarTone::Unusual);
	REQUIRE(SpanWith(mergedText, "merged") != nullptr);
	CHECK(SpanWith(mergedText, "merged")->Tone == SidebarTone::Merged);
}

TEST_CASE("Git window: a branch on another branch reads on top of it")
{
	LineReadBuilder builder;
	const std::size_t lower = builder.AddLocal("team/lower");
	const std::size_t upper = builder.AddLocal("upper");
	LineRead read = builder.Build();
	Core::BranchLine& line = LineOfRef(read, upper);
	line.Kind = Core::ComparedKind::Branch;
	line.ComparedWith = lower;
	line.Ahead = 3;

	const SidebarRowText text = Describe(read, BranchRow(upper));

	REQUIRE(SpanWith(text, "on top of lower") != nullptr);
	CHECK(SpanWith(text, "on top of lower")->Tone == SidebarTone::Unusual);
	CHECK(text.Tooltip.find("Built on team/lower: 3 ahead of team/lower") != std::string::npos);
}

TEST_CASE("Git window: a branch on a less common line names it")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t main = builder.AddRemoteBranch("origin", "main");
	const std::size_t release = builder.AddRemoteBranch("origin", "release/4.12");
	builder.MakeBase(main);
	builder.MakeBase(release);
	const std::size_t a = builder.AddLocal("a");
	const std::size_t b = builder.AddLocal("b");
	const std::size_t c = builder.AddLocal("c");
	LineRead read = builder.Build();

	for (const std::size_t ref : {a, b})
	{
		Core::BranchLine& line = LineOfRef(read, ref);
		line.Kind = Core::ComparedKind::Base;
		line.ComparedWith = main;
		line.Ahead = 2;
	}

	Core::BranchLine& odd = LineOfRef(read, c);
	odd.Kind = Core::ComparedKind::Base;
	odd.ComparedWith = release;
	odd.Ahead = 5;

	CHECK(MostCommonLineOf(read) == "refs/remotes/origin/main");
	CHECK(SpanWith(Describe(read, BranchRow(a)), "vs main") == nullptr);
	CHECK(SpanWith(Describe(read, BranchRow(c)), "vs release/4.12") != nullptr);
	CHECK(Describe(read, BranchRow(c)).Tooltip.find("5 ahead of release/4.12") != std::string::npos);
	CHECK(Describe(read, BranchRow(a)).Tooltip.find("2 ahead of main") != std::string::npos);
}

TEST_CASE("Git window: the most common line is empty when nothing is compared")
{
	LineReadBuilder builder;
	builder.AddLocal("a");
	LineRead read = builder.Build();

	CHECK(MostCommonLineOf(read).empty());
}

TEST_CASE("Git window: merged and empty branches say what they point into in the tooltip")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t develop = builder.AddRemoteBranch("origin", "develop");
	builder.MakeBase(develop);
	const std::size_t merged = builder.AddLocal("done", BranchSpec{.State = Core::BranchState::MergedByHistory});
	const std::size_t empty = builder.AddLocal("empty", BranchSpec{.State = Core::BranchState::NoOwnCommits});
	LineRead read = builder.Build();

	for (const std::size_t ref : {merged, empty})
	{
		Core::BranchLine& line = LineOfRef(read, ref);
		line.Kind = Core::ComparedKind::Base;
		line.ComparedWith = develop;
	}

	CHECK(Describe(read, BranchRow(merged)).Tooltip.find("Merged into develop") != std::string::npos);
	CHECK(Describe(read, BranchRow(empty)).Tooltip.find("No commits of its own yet; it points into develop") != std::string::npos);
}

TEST_CASE("Git window: an old copy of a default reads as one")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	builder.AddRemote("fork");
	const std::size_t current = builder.AddRemoteBranch("fork", "main");
	const std::size_t old = builder.AddRemoteBranch("origin", "main");
	builder.MakeBase(current);
	LineRead read = builder.Build();
	read.Model.OldCopies.push_back(Core::OldCopy{.Ref = old, .Of = current});

	const SidebarRowText text = Describe(read, BranchRow(old, SidebarSection::Remote));

	CHECK(SpanWith(text, "default here, an old copy of main") != nullptr);
}

TEST_CASE("Git window: history cut off and no common history are said in words")
{
	LineReadBuilder builder;
	const std::size_t cut = builder.AddLocal("cut", BranchSpec{.State = Core::BranchState::HistoryCutOff});
	const std::size_t orphan = builder.AddLocal("orphan", BranchSpec{.State = Core::BranchState::NoCommonHistory});
	const std::size_t missing = builder.AddLocal("missing", BranchSpec{.AgeDays = std::nullopt, .State = Core::BranchState::TipMissing});
	LineRead read = builder.Build();

	CHECK(SpanWith(Describe(read, BranchRow(cut)), "history cut off") != nullptr);
	CHECK(SpanWith(Describe(read, BranchRow(orphan)), "no common history") != nullptr);

	const SidebarRowText text = Describe(read, BranchRow(missing));
	CHECK(Texts(text) == Strings{"local only", "tip missing"});
}

TEST_CASE("Git window: push and pull counts read in words")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t remote = builder.AddRemoteBranch("origin", "x");
	const std::size_t both = builder.AddLocal("x", BranchSpec{.Ahead = 2, .Behind = 3, .DistanceKnown = true});
	builder.SameRow(both, remote);
	const std::size_t push = builder.AddLocal("p", BranchSpec{.Ahead = 2, .DistanceKnown = true});
	const std::size_t pull = builder.AddLocal("q", BranchSpec{.Behind = 3, .DistanceKnown = true});
	const std::size_t unknown = builder.AddLocal("u", BranchSpec{.Ahead = 9, .Behind = 9});
	LineRead read = builder.Build();

	CHECK(Describe(read, BranchRow(both)).Right == "2 to push \xC2\xB7 3 to pull");
	CHECK(Describe(read, BranchRow(push)).Right == "2 to push");
	CHECK(Describe(read, BranchRow(pull)).Right == "3 to pull");
	CHECK(Describe(read, BranchRow(unknown)).Right.empty());
	CHECK(Describe(read, BranchRow(both)).Tooltip.find("Local copy: 2 to push, 3 to pull") != std::string::npos);
}

TEST_CASE("Git window: a base row takes its push and pull words from its local copy")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t remote = builder.AddRemoteBranch("origin", "release/4.12");
	builder.MakeBase(remote);
	const std::size_t local = builder.AddLocal("release/4.12", BranchSpec{.Head = true, .Ahead = 1, .DistanceKnown = true});
	LineRead read = builder.Build();

	const SidebarRowText text = Describe(read, BaseRow(remote, local));

	CHECK(text.Head == "release/4.12");
	CHECK(text.Right == "1 to push");
	CHECK(text.IsHead);
	CHECK(text.Meta[0].Text == "local + origin");
	CHECK(text.Meta[0].Tone == SidebarTone::Both);
}

TEST_CASE("Git window: a base reads where it lives, its age, and whether the main line was guessed")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t remote = builder.AddRemoteBranch("origin", "main");
	const std::size_t localOnly = builder.AddLocal("trunk");
	builder.MakeBase(remote);
	builder.MakeBase(localOnly);
	LineRead read = builder.Build();
	BaseOfRef(read, remote).Reason = Core::BaseReason::MainLineGuess;
	BaseOfRef(read, localOnly).Reason = Core::BaseReason::MainLineByUser;

	const SidebarRowText guessed = Describe(read, BaseRow(remote));
	const SidebarRowText chosen = Describe(read, BaseRow(localOnly));

	CHECK(Texts(guessed) == Strings{"origin only", "4w ago", "guessed"});
	CHECK(guessed.Meta[0].Tone == SidebarTone::Dim);
	CHECK(guessed.Meta[2].Tone == SidebarTone::Unusual);
	CHECK(Texts(chosen) == Strings{"local only", "4w ago", "chosen by you"});
	CHECK(chosen.Meta[0].Tone == SidebarTone::Local);
	CHECK(guessed.Tooltip.find("Base: Guessed: most branches forked") != std::string::npos);
}

TEST_CASE("Git window: base reasons are worded one by one")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t remote = builder.AddRemoteBranch("origin", "main");
	builder.MakeBase(remote);
	LineRead read = builder.Build();

	Core::LineBase base;
	base.Ref = remote;

	base.Reason = Core::BaseReason::ServerDefault;
	CHECK(BaseReasonText(base, read) == "Default branch on origin");
	base.Reason = Core::BaseReason::MainLineByUser;
	CHECK(BaseReasonText(base, read) == "Chosen by you as the main line");
	base.Reason = Core::BaseReason::AddedByUser;
	CHECK(BaseReasonText(base, read) == "Added by you");
	base.Reason = Core::BaseReason::Kept;
	CHECK(BaseReasonText(base, read) == "A base once; it stays one until you stop it");

	base.Reason = Core::BaseReason::MergedLines;
	base.Qualities.MergedLines = 7;
	CHECK(BaseReasonText(base, read) == "7 different branches merged into it in the last 90 days");

	base.Reason = Core::BaseReason::Authors;
	base.Qualities.Authors = 4;
	base.Qualities.Span = 86400 * 41 + 500;
	CHECK(BaseReasonText(base, read) == "4 people worked on it over 41 days");

	base.Qualities.Span = 100;
	CHECK(BaseReasonText(base, read) == "4 people worked on it over 1 day");
}

TEST_CASE("Git window: a base with empty qualities is not described by zero counts")
{
	LineReadBuilder builder;
	builder.AddRemote("origin");
	const std::size_t remote = builder.AddRemoteBranch("origin", "main");
	builder.MakeBase(remote);
	LineRead read = builder.Build();

	Core::LineBase base;
	base.Ref = remote;
	base.Reason = Core::BaseReason::MergedLines;
	CHECK(BaseReasonText(base, read) == "Found from the data");

	base.Reason = Core::BaseReason::Authors;
	CHECK(BaseReasonText(base, read) == "Found from the data");
}
