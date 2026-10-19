#pragma once

struct ImFont;
struct ImFontAtlas;

namespace Core {

	inline constexpr float kInterfaceFontSize = 16.0f;
	inline constexpr float kCodeFontSize = 17.0f;
	inline constexpr float kBadgeFontSize = 11.0f;

	struct FontSet
	{
		ImFont* Interface = nullptr;
		ImFont* Code = nullptr;
		ImFont* Strong = nullptr;
		ImFont* Badge = nullptr;
	};

	FontSet LoadFontSet(ImFontAtlas& atlas);

	void PushInterfaceFont(float size = kInterfaceFontSize);

	void PushCodeFont(float size = kCodeFontSize);
}
