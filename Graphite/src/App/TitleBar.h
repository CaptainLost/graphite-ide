#pragma once

#include "Core/ImGui/CaptionLayout.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Core {

	class Window;

}

namespace Graphite {

	inline constexpr float kCaptionHeight = 32.0f;

	struct TitleContext
	{
		std::optional<std::string> Folder;
		std::optional<std::string> Branch;
	};

	std::string FolderLabel(const std::filesystem::path& root);

	Core::CaptionLayout DrawTitle(const TitleContext& context, float menuEnd, float barWidth);

	class TitleBar
	{
	public:
		void RenderLogo(Core::Window& window);

		void Render(Core::Window& window, const TitleContext& context);
	};
}
