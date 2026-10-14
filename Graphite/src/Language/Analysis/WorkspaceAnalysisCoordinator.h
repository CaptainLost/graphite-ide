#pragma once

#include "Editor/Overlays/AnalysisCache.h"
#include "Language/Analysis/AnalysisStore.h"
#include "Language/Analysis/AnalysisWalk.h"
#include "Language/Analysis/FileTextSource.h"
#include "Language/Analysis/WorkspaceAnalysis.h"

#include "Core/Text/Columns.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace Graphite {

	class WorkspaceServersPort
	{
	public:
		virtual ~WorkspaceServersPort() = default;

		WorkspaceServersPort() = default;
		WorkspaceServersPort(const WorkspaceServersPort&) = delete;
		WorkspaceServersPort& operator=(const WorkspaceServersPort&) = delete;

		virtual void Revalidate() = 0;

		virtual void WarmUp(const std::filesystem::path& root, bool respectGitIgnore) = 0;
	};

	struct AnalysisSwitches
	{
		const std::filesystem::path& Root;
		const std::filesystem::path& SelectedSolution;

		bool Trusted = false;

		bool SettingEnabled = true;

		std::uint64_t TrustRevision = 0;
	};

	struct AnalysisTickInputs
	{
		double Now = 0.0;

		bool ForegroundBusy = false;

		std::size_t BudgetBytes = 0;
		std::size_t Concurrency = 1;
		std::size_t TabWidth = Core::DefaultTabWidth;

		std::function<bool(const std::filesystem::path&)> HeldElsewhere;
	};

	// WARNING: Two calls a frame. Update must precede LanguageServices::Poll, which turns a
	// revalidation or warm-up into started servers on the same frame. Tick must follow Poll and
	// PreviewSemantics::Tick, because server readiness and document holders settle only by then.
	class WorkspaceAnalysisCoordinator
	{
	public:
		WorkspaceAnalysisCoordinator(WorkspaceServersPort& servers, WorkspaceAnalysisPort& port,
									 FileTextSource& texts, AnalysisCache& cache);

		WorkspaceAnalysisCoordinator(const WorkspaceAnalysisCoordinator&) = delete;
		WorkspaceAnalysisCoordinator& operator=(const WorkspaceAnalysisCoordinator&) = delete;

		void Update(const AnalysisSwitches& switches);

		void Tick(const AnalysisTickInputs& inputs);

		bool IsEnabled() const { return m_Enabled; }

		WorkspaceAnalysis& GetPass() { return m_Pass; }
		const WorkspaceAnalysis& GetPass() const { return m_Pass; }
		AnalysisStore& GetStore() { return m_Store; }
		const AnalysisStore& GetStore() const { return m_Store; }

	private:
		WorkspaceServersPort& m_Servers;
		AnalysisCache& m_Cache;

		// WARNING: m_Store must be declared before m_Pass, which holds a pointer to it and writes
		// into it until its own destructor has run.
		AnalysisStore m_Store;

		WorkspaceAnalysis m_Pass;

		// WARNING: m_Walk must stay last, so its walking thread is cancelled and joined before
		// anything it feeds goes away.
		AnalysisWalk m_Walk;

		bool m_Enabled = false;

		std::filesystem::path m_WarmedRoot;
		std::filesystem::path m_WalkedSolution;

		// WARNING: m_WasEnabled starts at the setting's own default, so a session loading that
		// default sees no edge on its first frame.
		bool m_WasEnabled = true;

		std::uint64_t m_SeenTrustRevision = 0;

		bool m_WasTrusted = false;
	};
}
