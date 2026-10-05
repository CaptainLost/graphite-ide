#pragma once

namespace Core {

	class Window;

}

namespace Graphite {

	// What Windows itself uses for a caption, so the drawn window buttons keep their
	// familiar hit size.
	inline constexpr float kCaptionHeight = 32.0f;

	class TitleBar
	{
	public:
		// The mark at the row's left edge, before the menus.
		void RenderLogo(Core::Window& window);

		void Render(Core::Window& window);
	};
}