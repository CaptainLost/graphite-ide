#pragma once

#include "Git/Work/ChipRow.h"
#include "Git/Work/WorkBoard.h"
#include "Review/ReviewFiles.h"

#include "Core/Base/Result.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace Graphite {

	class ActiveScheme;

	// The Git window's first view: cards for the work not yet in the trunk, above a read-only group of what is ready to
	// remove. The caller opens the window these scroll in.
	class WorkView
	{
	public:
		explicit WorkView(const ActiveScheme& scheme);

		std::function<void(const WorkCard& card)> OnReview;
		std::function<void()> OnChanges;

		// Core::Shell's folder opener.
		std::function<void(const std::filesystem::path& folder)> OnOpenFolder;

		// Fills the context menu of a branch card.
		std::function<void(const std::string& branchFullName)> DrawBranchMenu;

		// Empty while the count is unknown.
		std::function<std::optional<ViewedCount>(const std::string& branchFullName)> ReviewProgressOf;
		std::function<void()> OnRefresh;

		void Draw(const WorkBoard& board, bool reading, const std::optional<Core::Error>& error,
				  std::chrono::sys_seconds now);

		void ExpandReady();
		bool IsReadyExpanded() const { return m_ReadyExpanded; }

	private:
		void DrawBar(const WorkBoard& board, bool reading);
		void DrawCards(const WorkBoard& board, std::chrono::sys_seconds now);
		void DrawCard(const WorkBoard& board, const WorkCard& card, float width, float height,
					  std::chrono::sys_seconds now);
		void DrawTitle(const WorkCard& card);
		void DrawSubject(const std::string& subject);
		void DrawChips(const WorkCard& card, const std::string& trunk);
		void DrawProgress(const WorkCard& card);
		void DrawButtons(const WorkCard& card, std::chrono::sys_seconds now);
		void DrawReady(const WorkBoard& board, std::chrono::sys_seconds now);

		const ActiveScheme& m_Scheme;

		// Read once a frame, before the cards.
		ChipColours m_ChipColours;
		bool m_ReadyExpanded = false;
	};
}
