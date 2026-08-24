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
			entry.Items = published.Items;
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

		// A document with a version sent but no answer yet is not stale - it simply has nothing
		// to be stale about. Without this, a fresh entry born from a restart's version note
		// (Answered still at its default of "never") would read as stale against the very first
		// version it was ever given.
		if (it->second.Answered == DocumentVersion{})
		{
			return false;
		}

		return it->second.Answered < it->second.Sent;
	}

	DiagnosticsModel::Counts DiagnosticsModel::Total() const
	{
		Counts counts;

		for (const auto& [path, entry] : m_Documents)
		{
			for (const Diagnostic& diagnostic : entry.Items)
			{
				if (diagnostic.Severity == DiagnosticSeverity::Error)
				{
					++counts.Errors;
				}
				else
				{
					++counts.Warnings;
				}
			}
		}

		return counts;
	}
}
