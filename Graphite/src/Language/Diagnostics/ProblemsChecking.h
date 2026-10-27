#pragma once

#include "Language/Api/LanguageId.h"
#include "Language/Api/LanguageService.h"
#include "Language/Diagnostics/DiagnosticsModel.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Graphite {

	// WARNING: A server status reads NotConfigured both when no server is chosen for the language and when the chosen
	// one has not been created for this file's root yet, so ServerChosen tells the two apart.
	struct OpenDocumentCheck
	{
		std::filesystem::path Path;
		LanguageId Language = LanguageId::PlainText;
		ServerStatus Server;
		bool ServerChosen = false;
		const DiagnosticsModel::Entry* Entry = nullptr;
	};

	enum class DocumentCheck : std::uint8_t
	{
		Checked,
		Checking,
		WaitingForServer,
		ServerUnavailable,
		ServerGaveNoAnswer,
		NeverSent,
		NotSupported
	};

	DocumentCheck CheckOf(const OpenDocumentCheck& document);

	struct UncheckedDocument
	{
		std::filesystem::path Path;
		ServerStatus Server;
	};

	struct ProblemsChecking
	{
		std::vector<std::string> UncheckedLanguages;
		std::vector<UncheckedDocument> NotAnswered;
		std::vector<std::filesystem::path> NeverSent;
		std::vector<std::filesystem::path> StillChecking;

		std::optional<ServerStatus> WaitingFor;
		std::size_t DocumentsWaitingForServer = 0;

		bool AnyAnswered = false;

		bool EverythingChecked() const;
		bool AnyStillComing() const;
		bool AnyNotes() const;
	};

	ProblemsChecking SummariseChecking(const std::vector<OpenDocumentCheck>& documents);

	std::string FileNamesText(const std::vector<std::filesystem::path>& paths);

	std::string StillCheckingText(const ProblemsChecking& checking);

	std::string NoProblemsSoFarText(const ProblemsChecking& checking);

	std::string NothingCheckedYetText(const ProblemsChecking& checking);

	std::string NotAnsweredText(const UncheckedDocument& document);

	std::string NeverSentText(const ProblemsChecking& checking);
}
