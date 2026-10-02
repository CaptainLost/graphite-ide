#pragma once

#include "Core/Git/Requests.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace Graphite {

	// What the reader typed above the history column. Two boxes rather than one: the text
	// is matched against everything a commit says about itself, while a path narrows the
	// walk to one file — and a path typed into the text box would find only the commits
	// whose message happens to mention it.
	struct HistoryFilter
	{
		std::string Text;

		// A file name, a pattern, or a whole path — Core::NamesPath decides which by whether
		// there is a separator in it. Whitespace alone means no narrowing, which is not a
		// nicety: narrowing by file diffs every commit the walk passes, so a box the reader
		// has cleared must leave the query as cheap as it was before they typed.
		std::string Path;

		friend bool operator==(const HistoryFilter&, const HistoryFilter&) = default;
	};

	// The filter the reader is typing, and the decision of when it has stood still long
	// enough to be worth a query. Separate from the boxes that fill it and from the model
	// it eventually asks, so the rule — one query per settled change, and none for a change
	// that ends where it started — can be checked without a frame or a repository.
	class SettlingFilter
	{
	public:
		// How long the text must stand still. Longer than GitModel::kDebounceSeconds, and
		// deliberately not shared with it: that one coalesces a burst of file-change events
		// a machine produces in milliseconds, this one waits out a human between keystrokes.
		static constexpr float kSettleSeconds = 0.2f;

		// Called every frame with whatever the boxes hold. A value that has not changed
		// leaves the wait running, so holding a key down does not postpone the query
		// forever the way re-arming on every call would.
		void Set(HistoryFilter filter);

		// True on the single frame a change has settled — the caller's cue to query.
		// Compared against what was last asked for rather than merely consumed: text typed
		// and erased back to where it started needs no query at all, since the page on
		// screen already answers it. The first call after a change does not count its
		// seconds: they belong to the frame the change was typed in.
		bool Advance(float seconds);

		const HistoryFilter& Get() const { return m_Pending; }

		// Records that the filter as it stands has been queried by someone other than
		// Advance — picking a branch and reaching the bottom of a page both query with
		// whatever is typed, filter included. Without this the wait would run out a moment
		// later and send the same query again, walking the history twice for one gesture.
		void Accept();

		// Back to no filter, with nothing pending and nothing remembered as asked for.
		// Called when the workspace opens or closes, alongside the rest of the view's
		// per-repository state: a change left pending from the previous repository would
		// otherwise fire over the top of the new one's first page.
		void Reset();

	private:
		HistoryFilter m_Pending;

		// What Advance last handed out. Starts empty, matching the unfiltered first page
		// the view asks for on its own.
		HistoryFilter m_Requested;

		float m_Since = 0.0f;

		// Set changed the filter since the last Advance.
		bool m_JustSet = false;
	};

	// The query one column state asks for. A function rather than a few lines inside the
	// draw loop, because it is called from three places — picking a ref, typing in a box,
	// and reaching the bottom of a page — and a filter dropped from any one of them is a
	// filter that silently clears itself.
	//
	// `maxCount` absent leaves the request's own default page size; load-more is what
	// passes one.
	Core::LogRequest BuildLogRequest(const std::optional<std::string>& ref,
									 const HistoryFilter& filter,
									 std::optional<std::size_t> maxCount,
									 const std::optional<Core::Oid>& stash = std::nullopt);

	// Substring, case ignored, surrounding whitespace not part of the text. Matched
	// against the full name, so "origin" narrows to one remote's branches.
	//
	// The refs column filters in the view and never queries: every branch is already in
	// hand, unlike history, where the commits worth finding are usually the ones not yet
	// fetched.
	bool MatchesRefFilter(std::string_view name, std::string_view text);
}
