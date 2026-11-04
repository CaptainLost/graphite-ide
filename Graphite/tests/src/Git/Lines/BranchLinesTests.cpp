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
