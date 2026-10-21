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

	struct DiffViewOptions
	{
		DiffViewMode Mode = DiffViewMode::SideBySide;
		DiffScope Scope = DiffScope::Hunks;
		bool ColourSyntax = true;
		bool WordWrap = false;

		friend bool operator==(const DiffViewOptions&, const DiffViewOptions&) = default;
	};
}
