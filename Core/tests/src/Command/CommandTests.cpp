#include <doctest/doctest.h>

#include "Core/Command/Command.h"

using Core::Command;
using Core::IsCommandChecked;
using Core::IsCommandEnabled;

TEST_CASE("IsCommandEnabled counts a command without an Enabled predicate as enabled")
{
	const Command command{.Id = "editor.zoomIn", .Label = "Zoom In"};

	CHECK(IsCommandEnabled(command));
}

TEST_CASE("IsCommandEnabled answers with the command's Enabled predicate when it has one")
{
	bool workspaceOpen = false;
	const Command command{.Id = "workspace.close",
						  .Label = "Close Workspace",
						  .Enabled = [&workspaceOpen] { return workspaceOpen; }};

	CHECK_FALSE(IsCommandEnabled(command));

	workspaceOpen = true;

	CHECK(IsCommandEnabled(command));
}

TEST_CASE("IsCommandChecked counts a command without a Checked predicate as unchecked")
{
	const Command command{.Id = "editor.zoomIn", .Label = "Zoom In"};

	CHECK_FALSE(IsCommandChecked(command));
}

TEST_CASE("IsCommandChecked answers with the command's Checked predicate when it has one")
{
	bool wordWrapOn = true;
	const Command command{.Id = "editor.toggleWordWrap",
						  .Label = "Word Wrap",
						  .Checked = [&wordWrapOn] { return wordWrapOn; }};

	CHECK(IsCommandChecked(command));

	wordWrapOn = false;

	CHECK_FALSE(IsCommandChecked(command));
}
