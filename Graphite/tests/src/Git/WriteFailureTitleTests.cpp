#include <doctest/doctest.h>

#include "Git/WriteFailureTitle.h"

#include <string_view>

using Graphite::GitModel;
using Graphite::WriteFailureTitle;

TEST_CASE("Every kind of git write names its own failure")
{
	struct Case
	{
		GitModel::WriteKind Kind;
		std::string_view Title;
	};

	const Case cases[] = {
		{GitModel::WriteKind::Stage, "Stage failed"},
		{GitModel::WriteKind::Unstage, "Unstage failed"},
		{GitModel::WriteKind::Discard, "Discard failed"},
		{GitModel::WriteKind::Commit, "Commit failed"},
		{GitModel::WriteKind::CreateBranch, "Create branch failed"},
		{GitModel::WriteKind::RenameBranch, "Rename branch failed"},
		{GitModel::WriteKind::DeleteBranch, "Delete branch failed"},
		{GitModel::WriteKind::StashSave, "Stash failed"},
		{GitModel::WriteKind::StashApply, "Stash apply failed"},
		{GitModel::WriteKind::StashPop, "Stash pop failed"},
		{GitModel::WriteKind::StashDrop, "Stash drop failed"},
		{GitModel::WriteKind::Checkout, "Checkout failed"},
		{GitModel::WriteKind::FastForward, "Pull failed"},
	};

	for (const Case& testCase : cases)
	{
		CAPTURE(testCase.Title);
		CHECK(WriteFailureTitle(testCase.Kind) == testCase.Title);
	}
}

TEST_CASE("A kind whose failure never reaches the queue still gets a title")
{
	for (const GitModel::WriteKind kind :
		 {GitModel::WriteKind::Init, GitModel::WriteKind::DeleteCheck, GitModel::WriteKind::CheckoutPlan})
	{
		CHECK(WriteFailureTitle(kind) == "Git action failed");
	}
}
