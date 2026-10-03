#pragma once

#include "Editor/AnalysisCache.h"
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

	// What the coordinator needs from the language servers as a whole, and nothing more.
	//
	// Separate from LanguageServices for the reason WorkspaceAnalysisPort is: when the servers are
	// revalidated and warmed is a policy worth asserting without a clangd process.
	class WorkspaceServersPort
	{
	public:
		virtual ~WorkspaceServersPort() = default;

		WorkspaceServersPort() = default;
		WorkspaceServersPort(const WorkspaceServersPort&) = delete;
		WorkspaceServersPort& operator=(const WorkspaceServersPort&) = delete;

		// The trust list changed: stop servers whose root is now refused, start restricted ones
		// that are now permitted.
		virtual void Revalidate() = 0;

		virtual void WarmUp(const std::filesystem::path& root, bool respectGitIgnore) = 0;
	};

	// What Update reads each frame. Every field is a value the frame computes once and spends
	// everywhere, so the walk, the store and the pass cannot disagree about it.
	struct AnalysisSwitches
	{
		// Empty when no folder is open.
		const std::filesystem::path& Root;

		// The open folder is trusted. False when no folder is open.
		bool Trusted = false;

		// The background-analysis setting, before trust is folded in.
		bool SettingEnabled = true;

		// WorkspaceTrust::Revision: any change to the list, from anywhere.
		std::uint64_t TrustRevision = 0;
	};

	// What Tick reads each frame.
	struct AnalysisTickInputs
	{
		double Now = 0.0;

		// The active document still has questions outstanding, which the pass yields to.
		bool ForegroundBusy = false;

		std::size_t BudgetBytes = 0;
		std::size_t Concurrency = 1;
		std::size_t TabWidth = Core::DefaultTabWidth;

		// Whether somebody other than the pass already holds this path open with the server. See
		// WorkspaceAnalysis::Tick and ADR 0512.
		std::function<bool(const std::filesystem::path&)> HeldElsewhere;
	};

	// Keeps the background analysis of ADRs 0511 and 0513 in step with the folder, its trust
	// (ADR 0532) and the setting: owns the walk, the store and the pass, and turns frame-to-frame
	// changes in those three into the calls that start, restart and stop them.
	//
	// Changes are noticed by comparing against what was seen last frame rather than by hooking
	// the places a folder opens, a setting moves or trust is granted: a compare per frame catches
	// every way each can change, including ways added later, and costs a path compare and a few
	// integer compares against values that almost never differ.
	//
	// Two calls a frame, in this order and with LanguageServices::Poll between them. Update
	// reacts to the edges, and must precede Poll, because Poll is what turns a revalidation or a
	// warm-up into started servers on the same frame. Tick feeds and advances the pass, and must
	// follow Poll and PreviewSemantics::Tick, because it asks about servers' readiness and about
	// who holds a document, and both of those answers are settled for the frame only by then.
	class WorkspaceAnalysisCoordinator
	{
	public:
		WorkspaceAnalysisCoordinator(WorkspaceServersPort& servers, WorkspaceAnalysisPort& port,
									 FileTextSource& texts, AnalysisCache& cache);

		WorkspaceAnalysisCoordinator(const WorkspaceAnalysisCoordinator&) = delete;
		WorkspaceAnalysisCoordinator& operator=(const WorkspaceAnalysisCoordinator&) = delete;

		void Update(const AnalysisSwitches& switches);

		void Tick(const AnalysisTickInputs& inputs);

		// Whether the analysis runs this frame: the setting is on and the folder is trusted.
		bool IsEnabled() const { return m_Enabled; }

		// For DocumentWindows, which reads cached analyses back from both and routes answers to
		// the pass, and for the status bar, which shows the pass's progress.
		WorkspaceAnalysis& GetPass() { return m_Pass; }
		const WorkspaceAnalysis& GetPass() const { return m_Pass; }
		AnalysisStore& GetStore() { return m_Store; }
		const AnalysisStore& GetStore() const { return m_Store; }

	private:
		WorkspaceServersPort& m_Servers;
		AnalysisCache& m_Cache;

		// Ahead of the pass, which holds it as a pointer and writes every finished file into it
		// until its own destructor has run. Its worker is joined by its own destructor.
		AnalysisStore m_Store;

		WorkspaceAnalysis m_Pass;

		// Last, so its destructor runs first and the walking thread is cancelled and joined before
		// anything it feeds goes away. It holds no reference to the members above - its findings
		// are drained on the UI thread - so this is the cheap end of the order.
		AnalysisWalk m_Walk;

		// This frame's switch: the setting and trust together. Set by Update, read by Tick.
		bool m_Enabled = false;

		// The root the servers were last warmed and the walk last started for, so the comparison
		// in Update fires once per folder rather than once per frame.
		std::filesystem::path m_WarmedRoot;

		// The switch as it was last frame, so turning it back on restarts the walk. Starts at the
		// setting's own default, so a session that loads that default sees no edge on its first
		// frame.
		bool m_WasEnabled = true;

		// The trust list's revision last seen, so a change made anywhere - the bar, the menu, the
		// settings section - reaches the language servers through one compare per frame.
		std::uint64_t m_SeenTrustRevision = 0;

		// Whether the open folder was trusted last frame, for the edge that warms its servers.
		bool m_WasTrusted = false;
	};
}
