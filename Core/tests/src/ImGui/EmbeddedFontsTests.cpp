#include <doctest/doctest.h>

#include "Core/ImGui/EmbeddedFonts.h"

#include <imgui.h>

#include <array>

namespace {

	// A context without a backend: building the atlas needs no window.
	struct HeadlessContext
	{
		HeadlessContext() { ImGui::CreateContext(); }
		~HeadlessContext() { ImGui::DestroyContext(); }
	};

	ImFont* AddEmbedded(const Core::EmbeddedFont& font)
	{
		ImFontConfig config;
		config.FontDataOwnedByAtlas = false;

		return ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(
			font.CompressedData, font.CompressedSize, 15.0f, &config);
	}
}

TEST_CASE("Each embedded font is a face ImGui can load from memory")
{
	HeadlessContext context;

	CHECK(AddEmbedded(Core::JetBrainsMonoRegular()) != nullptr);
	CHECK(AddEmbedded(Core::JetBrainsMonoBold()) != nullptr);
	CHECK(AddEmbedded(Core::MaterialDesignIcons()) != nullptr);
	CHECK(AddEmbedded(Core::InterRegular()) != nullptr);
	CHECK(AddEmbedded(Core::InterSemiBold()) != nullptr);
}

TEST_CASE("The embedded text faces have the Polish letters the file names need")
{
	HeadlessContext context;
	ImFont* const fonts[] = {AddEmbedded(Core::JetBrainsMonoRegular()), AddEmbedded(Core::InterRegular())};

	constexpr std::array<ImWchar, 9> kPolishLetters = {0x0105, 0x0107, 0x0119, 0x0142, 0x0144, 0x00F3, 0x015B, 0x017A, 0x017C};

	for (ImFont* const font : fonts)
	{
		REQUIRE(font != nullptr);

		for (const ImWchar letter : kPolishLetters)
		{
			CHECK(font->IsGlyphInFont(letter));
		}
	}
}
