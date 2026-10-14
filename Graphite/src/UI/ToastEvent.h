#pragma once

#include "Core/ImGui/ToastStack.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Graphite {

	enum class ToastScope
	{
		Application,
		Folder
	};

	struct ToastEvent
	{
		Core::ToastKind Kind = Core::ToastKind::Error;
		std::string Title;
		std::string Message;
		std::string CopyText;

		ToastScope Scope = ToastScope::Application;
	};

	constexpr std::uint32_t ToastGroupOf(ToastScope scope)
	{
		constexpr std::uint32_t kApplicationToastGroup = 0u;
		constexpr std::uint32_t kFolderToastGroup = 1u;

		return scope == ToastScope::Folder ? kFolderToastGroup : kApplicationToastGroup;
	}

	ToastEvent FolderToast(Core::ToastKind kind, std::string title, std::string_view message,
						   const std::filesystem::path& root);

	ToastEvent ApplicationToast(Core::ToastKind kind, std::string title, std::string message);
}
