#include <doctest/doctest.h>

#include "Core/ImGui/EmbeddedFonts.h"
#include "Core/ImGui/Icons.h"
#include "Core/Text/Utf8.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <optional>

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
	CHECK(AddEmbedded(Core::Lucide()) != nullptr);
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

TEST_CASE("The embedded Lucide font holds every icon the interface draws")
{
	HeadlessContext context;
	ImFont* const lucide = AddEmbedded(Core::Lucide());
	REQUIRE(lucide != nullptr);

	for (const char* icon : Core::UsedIcons())
	{
		std::size_t offset = 0;
		const std::optional<char32_t> codepoint = Core::DecodeUtf8(icon, offset);
		REQUIRE(codepoint.has_value());
		CAPTURE(static_cast<unsigned int>(*codepoint));

		CHECK(lucide->IsGlyphInFont(static_cast<ImWchar>(*codepoint)));
	}
}
