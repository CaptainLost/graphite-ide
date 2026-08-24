#pragma once

// Lang/Api only. LanguageServices.h would drag LspClient.h, nlohmann/json and <thread> in
// behind it, and ProblemsPanel includes this header - see the ruling recorded against Task 3.
#include "Lang/Api/LanguageService.h"

#include <cstddef>
#include <filesystem>
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

		Counts Total() const;

		// Path-ordered, which is what makes the panel's grouping stable from frame to frame.
		const std::map<std::filesystem::path, Entry>& Documents() const { return m_Documents; }

	private:
		std::map<std::filesystem::path, Entry> m_Documents;
	};
}
