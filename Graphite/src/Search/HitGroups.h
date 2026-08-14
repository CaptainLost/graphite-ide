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
		// The full path in UTF-8. Never drawn — it is the tree node's id, and it has to stay the
		// same for the life of a search or the node closes itself every time a hit arrives.
		std::string Id;

		// What is drawn: the directory relative to the workspace root, dimmed, and the file name
		// after it. Computed once here rather than per frame, because every group is drawn on
		// every frame whether or not it is expanded.
		std::string Directory;
		std::string Name;

		// Kept because a jump needs the path, and rebuilding one from the pieces above would
		// undo the reason this holds them apart.
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
		// The workspace root every group's directory is shown relative to. Set when a search
		// starts; groups formed afterwards use it, so changing it mid-search would leave the
		// list showing two different kinds of path.
		void SetRoot(std::filesystem::path root);

		void Add(SearchHit hit);

		void Clear();

		const std::vector<HitGroup>& Get() const { return m_Groups; }

		std::size_t GetCount() const { return m_Count; }

	private:
		std::vector<HitGroup> m_Groups;

		// Name to index into m_Groups. Not an iterator or a pointer: m_Groups grows, and either
		// would dangle the moment it reallocated.
		std::unordered_map<std::string, std::size_t> m_Index;

		std::filesystem::path m_Root;

		std::size_t m_Count = 0;
	};
}
