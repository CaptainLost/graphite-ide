#pragma once

#include "Search/SearchQuery.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Graphite {

	struct HitGroup
	{
		// UTF-8, because that is what the results list draws and what its tree node is keyed on.
		std::string Name;

		// Kept beside the name because a jump needs the path and rebuilding one from the name
		// on every click would undo the reason this holds both.
		std::filesystem::path File;

		std::vector<SearchHit> Hits;
	};

	// Search hits, grouped by the file they were found in, built as they arrive.
	//
	// The panel used to regroup every hit into a fresh std::map on every frame. Measured on a
	// search returning 8000 hits across 400 files, that cost 5.3 ms per frame in a release
	// build — a third of a 60 Hz frame, spent before a single row was drawn, and spent whether
	// or not any file was expanded. Grouping as hits arrive makes the per-frame cost
	// proportional to what arrived that frame, which is nothing once a search finishes.
	//
	// Groups keep the order their first hit arrived in. The search runs as one job that walks
	// and scans in order, so that is the order the files were reached — stable between runs,
	// and the order a streaming list should show anyway.
	class HitGroups
	{
	public:
		void Add(SearchHit hit);

		void Clear();

		const std::vector<HitGroup>& Get() const { return m_Groups; }

		std::size_t GetCount() const { return m_Count; }

	private:
		std::vector<HitGroup> m_Groups;

		// Name to index into m_Groups. Not an iterator or a pointer: m_Groups grows, and either
		// would dangle the moment it reallocated.
		std::unordered_map<std::string, std::size_t> m_Index;

		std::size_t m_Count = 0;
	};
}
