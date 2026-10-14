#pragma once

#include "Language/Api/LanguageService.h"

#include <filesystem>
#include <string>

namespace Graphite {

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
