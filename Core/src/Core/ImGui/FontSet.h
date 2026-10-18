#pragma once

struct ImFont;
struct ImFontAtlas;

namespace Core {

	inline constexpr float kInterfaceFontSize = 14.0f;
	inline constexpr float kCodeFontSize = 15.0f;

	struct FontSet
	{
		ImFont* Interface = nullptr;
		ImFont* Code = nullptr;
		ImFont* Strong = nullptr;
	};

	FontSet LoadFontSet(ImFontAtlas& atlas);

	// WARNING: ImGui sets the style's FontSizeBase to the size of every push, so called inside
	// PushCodeFont this pushes the interface face at the code size.
	void PushInterfaceFont();

	void PushCodeFont();
}
