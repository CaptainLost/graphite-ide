#include "Language/Diagnostics/DiagnosticsModel.h"

namespace Graphite {

	namespace {
		using DocumentConstIterator = std::map<std::filesystem::path, DiagnosticsModel::Entry>::const_iterator;
	}

	void DiagnosticsModel::Apply(const DiagnosticsUpdate& update)
	{
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

			++entry.Generation;
		}
	}

	const std::vector<Diagnostic>& DiagnosticsModel::For(const std::filesystem::path& path) const
	{
		static const std::vector<Diagnostic> kEmpty;

		const DocumentConstIterator found = m_Documents.find(path);
		if (found == m_Documents.end())
		{
			return kEmpty;
		}

		const auto& [foundPath, entry] = *found;
		return entry.Items;
	}

	bool DiagnosticsModel::IsStale(const std::filesystem::path& path) const
	{
		const DocumentConstIterator found = m_Documents.find(path);
		if (found == m_Documents.end())
		{
			return false;
		}

		const auto& [foundPath, entry] = *found;

		if (!entry.HasAnswer)
		{
			return false;
		}

		return entry.Answered < entry.Sent;
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
