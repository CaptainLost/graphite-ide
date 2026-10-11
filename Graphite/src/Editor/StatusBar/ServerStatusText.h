#pragma once

#include "Language/Analysis/WorkspaceAnalysis.h"
#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"

#include <cstddef>
#include <string>

namespace Graphite {

	// Empty for NotConfigured: ChooseServerLine words a language without a choice, because only the language
	// knows whether a server could be chosen for it.
	std::string ServerStatusText(const ServerStatus& status);

	std::string ServerStatusDetail(const ServerStatus& status);

	// What the bar says while the file on screen still has questions outstanding with a server
	// that is otherwise up: its colours are still the lexer's own and its reference counts are
	// still blank, and "ready" answers about the server when the question is about the file.
	//
	// Falls through to ServerStatusText for any state other than Ready. A server still indexing
	// the whole project is the larger truth - this file's questions are outstanding because of
	// it - and naming the file there would bury the reason.
	std::string AnalysingText(const ServerStatus& status, const std::string& fileName);

	// Empty for PlainText: nobody opens a README expecting go to definition.
	std::string NoServerText(LanguageId language);

	std::string NoServerDetail(LanguageId language);

	// What the bar says about the pass reading the workspace behind the reader.
	//
	// Empty once the pass has finished, and empty before it has anything to report — the same
	// resting state the Problems slot has, where a count of nothing would be the one thing in the
	// bar that must be read to learn there is nothing to read.
	//
	// There is deliberately no phrase for "paused". The pass yields to the open document several
	// times a second, and a slot flickering between two states would read as a fault rather than
	// as the design working.
	std::string BackgroundAnalysisText(const AnalysisProgress& progress);

	// The tooltip behind it: the count again, and what the kept analysis weighs against the budget
	// it is allowed. Both figures are the ones the settings panel's slider is about, which is what
	// makes the number there mean something.
	std::string BackgroundAnalysisDetail(const AnalysisProgress& progress, std::size_t keptBytes,
										 std::size_t budgetBytes);

}
