#pragma once

namespace Graphite {

	enum class DiffViewMode
	{
		SideBySide,
		Unified
	};

	enum class DiffScope
	{
		Hunks,
		WholeFile
	};

	// How every diff is drawn: one set for the diff tabs, the review tabs and the Git panel's preview alike,
	// so a choice made in one of them holds in the next diff opened.
	struct DiffViewOptions
	{
		DiffViewMode Mode = DiffViewMode::SideBySide;
		DiffScope Scope = DiffScope::Hunks;
		bool ColourSyntax = true;
		bool WordWrap = false;

		friend bool operator==(const DiffViewOptions&, const DiffViewOptions&) = default;
	};
}
