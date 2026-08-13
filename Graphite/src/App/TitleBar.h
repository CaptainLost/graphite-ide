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
		void Render(Core::Window& window);
	};
}