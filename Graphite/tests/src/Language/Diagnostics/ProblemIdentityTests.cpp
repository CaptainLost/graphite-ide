#include <doctest/doctest.h>

#include "Language/Diagnostics/ProblemIdentity.h"

using namespace Graphite;

namespace {

	Diagnostic MakeDiagnostic(std::size_t line, std::size_t column, DiagnosticSeverity severity, const char* message)
	{
		Diagnostic diagnostic;
		diagnostic.Line = line;
		diagnostic.Column = Utf16Column{column};
		diagnostic.Severity = severity;
		diagnostic.Message = message;
		return diagnostic;
	}
}

TEST_CASE("A problem matches its own identity")
{
	const Diagnostic diagnostic = MakeDiagnostic(3, 4, DiagnosticSeverity::Warning, "unused");
	const ProblemIdentity identity = IdentityOf("a.cpp", diagnostic);

	CHECK(IsSameProblem(identity, "a.cpp", diagnostic));
}

TEST_CASE("A problem differing in file, position, severity or message does not match")
{
	const Diagnostic diagnostic = MakeDiagnostic(3, 4, DiagnosticSeverity::Warning, "unused");
	const ProblemIdentity identity = IdentityOf("a.cpp", diagnostic);

	CHECK_FALSE(IsSameProblem(identity, "b.cpp", diagnostic));
	CHECK_FALSE(IsSameProblem(identity, "a.cpp", MakeDiagnostic(4, 4, DiagnosticSeverity::Warning, "unused")));
	CHECK_FALSE(IsSameProblem(identity, "a.cpp", MakeDiagnostic(3, 5, DiagnosticSeverity::Warning, "unused")));
	CHECK_FALSE(IsSameProblem(identity, "a.cpp", MakeDiagnostic(3, 4, DiagnosticSeverity::Error, "unused")));
	CHECK_FALSE(IsSameProblem(identity, "a.cpp", MakeDiagnostic(3, 4, DiagnosticSeverity::Warning, "other")));
}
