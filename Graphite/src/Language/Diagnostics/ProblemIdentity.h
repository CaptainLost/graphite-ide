#pragma once

#include "Language/Api/LanguageService.h"

#include <filesystem>
#include <string>

namespace Graphite {

	// Which problem a list row stands for, by what the problem says rather than by where it
	// sits in the list. A refresh re-sorts and re-numbers the rows, so a position would point at
	// a different problem afterwards; the file, position, severity and message do not move
	// unless the problem itself changed.
	struct ProblemIdentity
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		std::size_t Column = 0;
		DiagnosticSeverity Severity = DiagnosticSeverity::Error;
		std::string Message;
	};

	inline ProblemIdentity IdentityOf(const std::filesystem::path& path, const Diagnostic& diagnostic)
	{
		return ProblemIdentity{path, diagnostic.Line, diagnostic.Column.Value, diagnostic.Severity,
							   diagnostic.Message};
	}

	inline bool IsSameProblem(const ProblemIdentity& identity, const std::filesystem::path& path,
							  const Diagnostic& diagnostic)
	{
		return identity.Path == path && identity.Line == diagnostic.Line &&
			   identity.Column == diagnostic.Column.Value && identity.Severity == diagnostic.Severity &&
			   identity.Message == diagnostic.Message;
	}
}
