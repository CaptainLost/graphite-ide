#pragma once

// Lang/Api only. LanguageServices.h would drag LspClient.h, nlohmann/json and <thread> in
// behind it, and ProblemsPanel includes this header - see the ruling recorded against Task 3.
#include "Lang/Api/LanguageService.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <vector>

namespace Graphite {

	// What every language server has said is wrong with the documents it holds open, in the
	// coordinates the protocol speaks - a line, a UTF-16 column, a version.
	//
	// Deliberately free of ImGui, of Core::TextBuffer and of any tab width: two readers want
	// different things from it. The panel lists it as it stands; the editor turns one document's
	// worth into DiagnosticOverlay, which is where the conversion into drawable coordinates
	// belongs because that is where the buffer is.
	//
	// Owned by GraphiteLayer and fed once a frame, immediately after LanguageServices::Poll and
	// before anything draws. Fed anywhere later and the panel reads, on the frame a diagnostic
	// arrives, a version of the truth the editor has already drawn past.
	class DiagnosticsModel
	{
	public:
		struct Entry
		{
			// The version last sent to the server, and the version the answer describes. Two
			// numbers rather than a bool because staleness is a comparison made when it is
			// asked for: a set that is fresh when it lands goes stale on the next keystroke, so
			// a flag written at delivery would be wrong within a frame.
			DocumentVersion Sent;
			DocumentVersion Answered;

			// Whether a publication has landed at all. DocumentVersion{} is both "never sent" and
			// what a reply naming no version reads as (see its own comment), so Answered alone
			// cannot tell a document that has never been answered from one a server answered
			// without naming a version - and those two cases must read oppositely: the first is
			// not stale, having nothing to be stale about; the second is stale as soon as anything
			// has been sent, which is the safe reading DocumentVersion's own comment commits to.
			bool HasAnswer = false;

			// Bumped by Apply every time this entry's Items are replaced, regardless of what
			// Answered says. Two publications can carry the same version and different content -
			// a server that omits version publishing twice, or clangd republishing once its
			// preamble build finishes - and a reader keyed on Answered would call them identical.
			// Generation never repeats, so a reader comparing it can tell "nothing changed since
			// last I looked" from "something did" even when the version cannot.
			std::uint64_t Generation = 0;

			std::vector<Diagnostic> Items;
		};

		struct Counts
		{
			std::size_t Errors = 0;
			std::size_t Warnings = 0;
		};

		// Applies closes, then versions, then publications. The order is fixed and matters: a
		// restart carries a close and the version that replaced it in one update, and applying
		// the version first would leave it attached to a document the close then forgets.
		void Apply(const DiagnosticsUpdate& update);

		// Empty for a document nothing has been said about, which is every document until a
		// server answers. A reference rather than a pointer so a caller loops over nothing
		// instead of branching on whether a server exists.
		const std::vector<Diagnostic>& For(const std::filesystem::path& path) const;

		// The buffer has moved on from the text these describe. What the editor dims on and the
		// panel marks; never a reason to drop anything.
		bool IsStale(const std::filesystem::path& path) const;

		// Errors and warnings summed across only the documents `include` admits — the model
		// knows nothing about tabs, so it is the caller's predicate that decides what "counts"
		// means. There is deliberately no unfiltered overload: the model also holds entries for
		// files opened by preview panes and by the language layer's warm-up, and a caller that
		// forgot to filter would silently count those in. The panel and the status bar both pass
		// the same "Core::DocumentSet::Find answers for this path" predicate, so the number shown
		// in the status bar always covers exactly the rows the panel lists — one rule, applied by
		// both, rather than two counts that can drift apart.
		Counts Total(const std::function<bool(const std::filesystem::path&)>& include) const;

		// Path-ordered, which is what makes the panel's grouping stable from frame to frame.
		const std::map<std::filesystem::path, Entry>& GetDocuments() const { return m_Documents; }

	private:
		std::map<std::filesystem::path, Entry> m_Documents;
	};
}
