#include <doctest/doctest.h>

#include "Git/Lines/BranchLines.h"

#include "Git/Lines/LineReader.h"

#include "Core/Git/Lines/LineModel.h"

#include <optional>
#include <string>
#include <utility>

using namespace Graphite;

namespace {
	Core::LineRef RefNamed(std::string fullName)
	{
		Core::LineRef ref;
		ref.FullName = std::move(fullName);
		return ref;
	}

	// origin/main is the base and main its local copy; feature is built on origin/main, topic on feature, lonely is
	// compared with nothing, and old/main is an old copy of origin/main.
	LineRead ReadOfSixRefs()
	{
		LineRead read;
		read.Refs = {RefNamed("refs/heads/main"),          RefNamed("refs/heads/feature"),       RefNamed("refs/heads/topic"),
					 RefNamed("refs/heads/lonely"),        RefNamed("refs/remotes/origin/main"), RefNamed("refs/remotes/old/main")};

		read.Model.Bases = {Core::LineBase{.Ref = 4, .Reason = Core::BaseReason::ServerDefault}};
		read.Model.OldCopies = {Core::OldCopy{.Ref = 5, .Of = 4}};
		read.Model.Branches = {
			Core::BranchLine{.Ref = 0, .Kind = Core::ComparedKind::Base, .ComparedWith = 4, .State = Core::BranchState::NoOwnCommits, .SameRowAs = 4},
			Core::BranchLine{.Ref = 1, .Kind = Core::ComparedKind::Base, .ComparedWith = 4, .Ahead = 2},
			Core::BranchLine{.Ref = 2, .Kind = Core::ComparedKind::Branch, .ComparedWith = 1, .Ahead = 1},
			Core::BranchLine{.Ref = 3, .Kind = Core::ComparedKind::None, .ByAnswer = true, .State = Core::BranchState::NotCompared},
			Core::BranchLine{.Ref = 5, .Kind = Core::ComparedKind::Base, .ComparedWith = 4, .State = Core::BranchState::MergedByHistory},
		};
		return read;
	}
}

TEST_CASE("Git window: branch lines find what a branch is compared with")
{
	const LineRead read = ReadOfSixRefs();
	const BranchLines lines{read};

	CHECK(lines.IndexOf("refs/heads/topic") == std::optional<std::size_t>{2});
	CHECK(lines.IndexOf("refs/remotes/old/main") == std::optional<std::size_t>{5});
	CHECK_FALSE(lines.IndexOf("refs/heads/gone").has_value());

	const Core::BranchLine* feature = lines.LineOf("refs/heads/feature");
	REQUIRE(feature != nullptr);
	CHECK(feature->Ref == 1);
	CHECK(feature->Ahead == 2);
	CHECK(lines.LineOf("refs/remotes/origin/main") == nullptr);
	CHECK(lines.LineOf("refs/heads/gone") == nullptr);

	const Core::LineBase* base = lines.BaseNamed("refs/remotes/origin/main");
	REQUIRE(base != nullptr);
	CHECK(base->Ref == 4);
	CHECK(lines.BaseNamed("refs/heads/main") == nullptr);

	const Core::OldCopy* copy = lines.OldCopyNamed("refs/remotes/old/main");
	REQUIRE(copy != nullptr);
	CHECK(copy->Of == 4);
	CHECK(lines.OldCopyNamed("refs/remotes/origin/main") == nullptr);

	CHECK(lines.ComparedWithName("refs/heads/feature") == std::optional<std::string>{"refs/remotes/origin/main"});
	CHECK(lines.ComparedWithName("refs/heads/topic") == std::optional<std::string>{"refs/heads/feature"});
	CHECK(lines.ComparedWithName("refs/heads/main") == std::optional<std::string>{"refs/remotes/origin/main"});
	CHECK_FALSE(lines.ComparedWithName("refs/heads/lonely").has_value());
	CHECK_FALSE(lines.ComparedWithName("refs/remotes/origin/main").has_value());
	CHECK_FALSE(lines.ComparedWithName("refs/heads/gone").has_value());
}

TEST_CASE("Git window: a base's local copy is shown as the base until it has commits of its own or a picked line")
{
	LineRead read = ReadOfSixRefs();

	CHECK(BranchLines{read}.ShownAs("refs/heads/main") == "refs/remotes/origin/main");
	CHECK(BranchLines{read}.ShownAs("refs/heads/feature") == "refs/heads/feature");
	CHECK(BranchLines{read}.ShownAs("refs/remotes/origin/main") == "refs/remotes/origin/main");
	CHECK(BranchLines{read}.ShownAs("refs/heads/gone") == "refs/heads/gone");

	read.Model.Branches[0].ByAnswer = true;
	CHECK(BranchLines{read}.ShownAs("refs/heads/main") == "refs/heads/main");

	read.Model.Branches[0].ByAnswer = false;
	read.Model.Branches[0].State = Core::BranchState::NotMerged;
	CHECK(BranchLines{read}.ShownAs("refs/heads/main") == "refs/heads/main");
}

TEST_CASE("Git window: a base's pick is read from the answers and an old copy's from its line")
{
	LineRead read = ReadOfSixRefs();

	CHECK_FALSE(BranchLines{read}.PickedLineOf("refs/remotes/origin/main").has_value());
	CHECK_FALSE(BranchLines{read}.PickedLineOf("refs/remotes/old/main").has_value());

	read.Answers.Comparisons["refs/remotes/origin/main"] = "refs/heads/feature";
	CHECK(BranchLines{read}.PickedLineOf("refs/remotes/origin/main") == std::optional<std::size_t>{1});

	read.Answers.Comparisons["refs/remotes/origin/main"] = "refs/heads/gone";
	CHECK_FALSE(BranchLines{read}.PickedLineOf("refs/remotes/origin/main").has_value());

	read.Answers.Comparisons["refs/remotes/origin/main"] = std::nullopt;
	CHECK_FALSE(BranchLines{read}.PickedLineOf("refs/remotes/origin/main").has_value());

	read.Model.Branches[4].ByAnswer = true;
	read.Model.Branches[4].ComparedWith = 1;
	CHECK(BranchLines{read}.PickedLineOf("refs/remotes/old/main") == std::optional<std::size_t>{1});

	// A branch's pick is its line's, which the model has applied already.
	read.Answers.Comparisons["refs/heads/feature"] = "refs/heads/topic";
	CHECK_FALSE(BranchLines{read}.PickedLineOf("refs/heads/feature").has_value());
}

TEST_CASE("Git window: what a history is compared with is a pick first, then a base or an old copy, then the branch's line")
{
	LineRead read = ReadOfSixRefs();

	const ShownComparison feature = BranchLines{read}.ComparisonOf("refs/heads/feature");
	CHECK(feature.What == ShownComparison::Kind::Line);
	CHECK(feature.Shown == "refs/heads/feature");
	REQUIRE(feature.Line != nullptr);
	CHECK(feature.Line->Ref == 1);

	// A branch compared with nothing still has a line of the model's; the line says so.
	CHECK(BranchLines{read}.ComparisonOf("refs/heads/lonely").What == ShownComparison::Kind::Line);

	const ShownComparison base = BranchLines{read}.ComparisonOf("refs/remotes/origin/main");
	CHECK(base.What == ShownComparison::Kind::Base);
	REQUIRE(base.Base != nullptr);
	CHECK(base.Base->Ref == 4);
	CHECK(base.Line == nullptr);

	// The base's local copy with no commits of its own stands for the base.
	const ShownComparison main = BranchLines{read}.ComparisonOf("refs/heads/main");
	CHECK(main.What == ShownComparison::Kind::Base);
	CHECK(main.Shown == "refs/remotes/origin/main");

	// The model gives the old copy a line; the copy is still compared with nothing.
	const ShownComparison copy = BranchLines{read}.ComparisonOf("refs/remotes/old/main");
	CHECK(copy.What == ShownComparison::Kind::OldCopy);
	REQUIRE(copy.OldCopy != nullptr);
	CHECK(copy.OldCopy->Of == 4);
	CHECK(copy.Line == nullptr);

	CHECK(BranchLines{read}.ComparisonOf("refs/heads/gone").What == ShownComparison::Kind::Nothing);

	read.Answers.Comparisons["refs/remotes/origin/main"] = "refs/heads/feature";
	const ShownComparison picked = BranchLines{read}.ComparisonOf("refs/heads/main");
	CHECK(picked.What == ShownComparison::Kind::Pick);
	CHECK(picked.Picked == std::optional<std::size_t>{1});
	CHECK(picked.Base == nullptr);

	read.Model.Branches[4].ByAnswer = true;
	read.Model.Branches[4].ComparedWith = 1;
	const ShownComparison copyPicked = BranchLines{read}.ComparisonOf("refs/remotes/old/main");
	CHECK(copyPicked.What == ShownComparison::Kind::Pick);
	CHECK(copyPicked.Picked == std::optional<std::size_t>{1});
}
