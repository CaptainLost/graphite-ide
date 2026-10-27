#pragma once

#include "Language/Api/LanguageService.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <vector>

namespace Graphite {

	class DiagnosticsModel
	{
	public:
		struct Entry
		{
			DocumentVersion Sent;
			DocumentVersion Answered;

			// WARNING: DocumentVersion{} means both "never sent" and "a reply named no version",
			// so Answered alone cannot tell an unanswered document (never stale) from one
			// answered without a version (stale once anything is sent).
			bool HasAnswer = false;

			// WARNING: Two publications can carry the same version and different content, so a
			// reader keyed on Answered would call them identical. Generation moves on every one.
			std::uint64_t Generation = 0;

			DocumentVersion GaveUpOn;

			std::vector<Diagnostic> Items;
		};

		struct Counts
		{
			std::size_t Errors = 0;
			std::size_t Warnings = 0;
		};

		// WARNING: Closes, then versions, then publications. A restart carries a close and the
		// version that replaced it in one update; applying the version first would attach it to a
		// document the close then forgets.
		void Apply(const DiagnosticsUpdate& update);

		const std::vector<Diagnostic>& For(const std::filesystem::path& path) const;

		bool IsStale(const std::filesystem::path& path) const;

		// WARNING: There is deliberately no unfiltered overload. The model also holds entries for
		// files opened by preview panes and by warm-up, which a caller that forgot to filter
		// would silently count.
		Counts Total(const std::function<bool(const std::filesystem::path&)>& include) const;

		const std::map<std::filesystem::path, Entry>& GetDocuments() const { return m_Documents; }

	private:
		std::map<std::filesystem::path, Entry> m_Documents;
	};
}
