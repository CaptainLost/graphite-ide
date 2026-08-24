#include "Lang/Diagnostics/DiagnosticsModel.h"

namespace Graphite {

	void DiagnosticsModel::Apply(const DiagnosticsUpdate& update)
	{
		// Closes first, so a restart's close-then-reopen within one update forgets the old
		// document before the new version note recreates it - see the class comment on order.
		for (const std::filesystem::path& path : update.Closed)
		{
			m_Documents.erase(path);
		}

		for (const DocumentVersionNote& note : update.Versions)
		{
			m_Documents[note.Path].Sent = note.Version;
		}

		for (const PublishedDiagnostics& published : update.Published)
		{
			Entry& entry = m_Documents[published.Path];
			entry.Answered = published.Version;
			entry.HasAnswer = true;
			entry.Items = published.Items;

			// Moves on every publication, not only ones that change Answered - see the comment
			// on Entry::Generation for why Answered alone cannot stand in for this.
			++entry.Generation;
		}
	}

	const std::vector<Diagnostic>& DiagnosticsModel::For(const std::filesystem::path& path) const
	{
		static const std::vector<Diagnostic> kEmpty;

		const auto it = m_Documents.find(path);
		if (it == m_Documents.end())
		{
			return kEmpty;
		}

		return it->second.Items;
	}

	bool DiagnosticsModel::IsStale(const std::filesystem::path& path) const
	{
		const auto it = m_Documents.find(path);
		if (it == m_Documents.end())
		{
			return false;
		}

		// A document with a version sent but no answer at all yet is not stale - it simply has
		// nothing to be stale about. Without this, a fresh entry born from a restart's version
		// note would read as stale against the very first version it was ever given. This is
		// distinct from a genuine answer that names no version: HasAnswer tells those apart,
		// because DocumentVersion{} means both things and comparing on the value alone cannot.
		if (!it->second.HasAnswer)
		{
			return false;
		}

		return it->second.Answered < it->second.Sent;
	}

	DiagnosticsModel::Counts DiagnosticsModel::Total(
		const std::function<bool(const std::filesystem::path&)>& include) const
	{
		Counts counts;

		for (const auto& [path, entry] : m_Documents)
		{
			if (!include(path))
			{
				continue;
			}

			for (const Diagnostic& diagnostic : entry.Items)
			{
				// Exhaustive rather than if/else: a third severity added to the enum without a
				// case here fails the build instead of silently counting as a warning.
				switch (diagnostic.Severity)
				{
				case DiagnosticSeverity::Error:
					++counts.Errors;
					break;
				case DiagnosticSeverity::Warning:
					++counts.Warnings;
					break;
				}
			}
		}

		return counts;
	}
}
