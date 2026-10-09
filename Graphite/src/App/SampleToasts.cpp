#include "App/SampleToasts.h"

#include "Core/FileSystem/FileSystem.h"

#include <format>
#include <string>

namespace Graphite {

	namespace {
		constexpr int kLongErrorLineCount = 12;

		std::string LongErrorMessage()
		{
			std::string message;
			for (int lineNumber = 1; lineNumber <= kLongErrorLineCount; ++lineNumber)
			{
				const bool lastLine = lineNumber == kLongErrorLineCount;
				message += std::format("remote: hunk {} of src/Notifications/NotificationsPopup.cpp conflicts with 'origin/main'",
									   lineNumber);
				if (!lastLine)
				{
					message += '\n';
				}
			}
			return message;
		}
	}

	ToastEvent SampleToast(SampleToastKind sampleKind, const std::filesystem::path& openFolder)
	{
		switch (sampleKind)
		{
		case SampleToastKind::Success:
			return ApplicationToast(Core::ToastKind::Success, "Copied", "Path: src/App/Main.cpp");
		case SampleToastKind::Warning:
			return ApplicationToast(Core::ToastKind::Warning, "Save skipped", "'Main.cpp' changed on disk since it was opened");
		case SampleToastKind::Error:
			return ApplicationToast(Core::ToastKind::Error, "Push failed", "rejected: 'main' is behind 'origin/main'");
		case SampleToastKind::LongError:
			return ApplicationToast(Core::ToastKind::Error, "Pull failed", LongErrorMessage());
		case SampleToastKind::FolderError:
			break;
		}

		const std::filesystem::path lockFile = openFolder / ".git" / "index.lock";
		return FolderToast(Core::ToastKind::Error, "Commit failed",
						   std::format("Unable to create '{}': File exists.", Core::ToUtf8(lockFile)), openFolder);
	}
}
