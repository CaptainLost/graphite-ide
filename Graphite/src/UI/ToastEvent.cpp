#include "UI/ToastEvent.h"

#include "UI/RelativePaths.h"

#include <utility>

namespace Graphite {

	ToastEvent FolderToast(Core::ToastKind kind, std::string title, std::string_view message,
						   const std::filesystem::path& root)
	{
		std::string copied = title + "\n" + std::string(message);

		return ToastEvent{.Kind = kind,
						  .Title = std::move(title),
						  .Message = RelativiseUnder(message, root),
						  .CopyText = std::move(copied),
						  .Scope = ToastScope::Folder};
	}

	ToastEvent ApplicationToast(Core::ToastKind kind, std::string title, std::string message)
	{
		return ToastEvent{.Kind = kind,
						  .Title = std::move(title),
						  .Message = std::move(message),
						  .Scope = ToastScope::Application};
	}
}
