#pragma once

#include "Git/Diff/DiffView.h"
#include "Git/GitModel.h"
#include "Theme/GitPalette.h"
#include "UI/TabWindow.h"

#include "Persistence/PersistedState.h"

#include "Core/Git/Oid.h"
#include "Core/Git/Requests.h"
#include "Core/Git/Status.h"
#include "Core/ImGui/TabCloseRequests.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Graphite {

	struct OpenDiff
	{
		std::optional<Core::Oid> Commit;
		Core::DiffTarget Target = Core::DiffTarget::CommitToParent;
		std::filesystem::path Path;

		friend bool operator==(const OpenDiff&, const OpenDiff&) = default;
	};

	// WARNING: ImGui keys the window by this string, and a window's dock position and imgui.ini
	// entry hang off it, so it must stay stable across frames and releases.
	std::string DiffWindowIdentity(const OpenDiff& diff);

	std::string DiffWindowTitle(const OpenDiff& diff);

	std::string DiffWindowName(const OpenDiff& diff);

	// WARNING: Read off the status and not off an empty diff: an empty diff is also what a tab
	// shows while the first query for a side is in flight, and following on that would send
	// the tab to the other side and back again on every switch.
	std::optional<Core::DiffTarget> TargetHolding(const Core::WorkingTreeStatus& status,
												  const std::filesystem::path& path,
												  Core::DiffTarget current);

	std::string EmptyDiffMessage(const OpenDiff& diff);

	// "Couldn't read the diff of a1b2c3d: <reason>" while the diff the tab shows stands failed, and nothing while it
	// loads or has loaded.
	std::optional<std::string> DiffFailureOf(const OpenDiff& diff, const GitModel& model);

	// Forgets the failure first, so the loader shows from this frame on rather than the old failure.
	void RetryDiff(const OpenDiff& diff, GitModel& model);

	class DiffWindows
	{
	public:
		void Open(const OpenDiff& diff);

		bool IsOpen(const OpenDiff& diff) const;
		std::size_t GetCount() const { return m_Open.size(); }

		void CloseAll();

		// WARNING: There is at most one working-tree tab, since GitModel keeps a single slot for
		// the working-tree diff. A caller that opens a new working-tree tab calls this
		// immediately before Open() so the previous tab does not ask into the new tab's slot.
		void CloseWorkingTree();

		std::vector<DiffTabState> CaptureState() const;

		void RestoreState(const std::vector<DiffTabState>& diffs);

		void Render(GitModel& model, const GitPalette& palette, const SyntaxPalette& syntax,
					ImU32 selection, const DiffViewOptions& options, unsigned int centreNode,
					Core::TabCloseRequests& closeRequests);

		std::function<void(std::string_view)> OnCopied;

		std::function<void(const DiffViewOptions&)> OnSetOptions;

		std::function<void(const std::filesystem::path&)> OpenFile;

		// WARNING: The returned function holds this and `model` by reference, so it must not
		// outlive either; reassign it each frame, as the working directory moves with the folder.
		std::function<void(const std::filesystem::path&)> OpenFileIn(const GitModel& model) const;

	private:
		struct Entry;

		static void FollowFile(Entry& entry, const GitModel& model);

		struct Entry
		{
			OpenDiff Diff;
			DiffView View;

			TabWindowState Window;

			// WARNING: The commit of a restored tab may be gone, rebased away or on a deleted
			// branch, and its tab would only say its diff is no longer loaded. So the first
			// answer decides: a diff clears this, a failure closes the tab.
			bool AwaitingFirstAnswer = false;

			FileTextsBinding Texts;
		};

		std::vector<Entry> m_Open;
	};
}
