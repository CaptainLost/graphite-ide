#pragma once

#include "UI/ToastEvent.h"

#include <filesystem>
#include <string_view>

namespace Graphite {

	enum class SampleToastKind
	{
		Success,
		Warning,
		Error,
		LongError,
		FolderError
	};

	struct SampleToastCommand
	{
		SampleToastKind Kind = SampleToastKind::Success;
		std::string_view CommandId;
		std::string_view Label;
		std::string_view ListLabel;
	};

	// In the order the Debug menu's Show Toast lists them.
	inline constexpr SampleToastCommand kSampleToastCommands[] = {
		{.Kind = SampleToastKind::Success, .CommandId = "debug.toast.success", .Label = "Success", .ListLabel = "Debug: Show a success toast"},
		{.Kind = SampleToastKind::Warning, .CommandId = "debug.toast.warning", .Label = "Warning", .ListLabel = "Debug: Show a warning toast"},
		{.Kind = SampleToastKind::Error, .CommandId = "debug.toast.error", .Label = "Error", .ListLabel = "Debug: Show an error toast"},
		{.Kind = SampleToastKind::LongError, .CommandId = "debug.toast.longError", .Label = "Long error", .ListLabel = "Debug: Show a long error toast"},
		{.Kind = SampleToastKind::FolderError, .CommandId = "debug.toast.folderError", .Label = "Folder error", .ListLabel = "Debug: Show a folder error toast"},
	};

	// The folder one is built for the folder open now, so its path reads relative to it as a real git failure's does.
	ToastEvent SampleToast(SampleToastKind sampleKind, const std::filesystem::path& openFolder);
}
