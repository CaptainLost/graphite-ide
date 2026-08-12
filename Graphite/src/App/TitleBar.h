#pragma once

namespace Core {

	class Window;

}

namespace Graphite {

	inline constexpr float kCaptionHeight = 32.0f;

	class TitleBar
	{
	public:
		void Render(Core::Window& window);
	};
}