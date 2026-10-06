#include <doctest/doctest.h>

#include "Core/ImGui/TextMetrics.h"

#include <imgui.h>

#include <string_view>

TEST_CASE("TextWidth measures exactly the bytes of the view, and nothing for an empty one")
{
	ImGuiContext* context = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(800.0f, 600.0f);
	io.IniFilename = nullptr;
	io.Fonts->Build();
	ImGui::NewFrame();

	CHECK(Core::TextWidth(std::string_view{}) == 0.0f);
	CHECK(Core::TextWidth("") == 0.0f);

	const std::string_view text = "ab##cd";
	CHECK(Core::TextWidth(text) == ImGui::CalcTextSize(text.data(), text.data() + text.size()).x);
	CHECK(Core::TextWidth(text.substr(0, 2)) == ImGui::CalcTextSize("ab").x);

	ImGui::EndFrame();
	ImGui::DestroyContext(context);
}

TEST_CASE("MeasureGlyphInk finds a narrow glyph's pixels inside its advance")
{
	ImGuiContext* context = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(800.0f, 600.0f);
	io.IniFilename = nullptr;
	io.Fonts->Build();
	ImGui::NewFrame();

	const Core::GlyphInk barInk = Core::MeasureGlyphInk("|");
	const float barAdvance = ImGui::CalcTextSize("|").x;

	CHECK(barInk.StartFromDrawX > 0.0f);
	CHECK(barInk.StartFromDrawX < barInk.EndFromDrawX);
	CHECK(barInk.EndFromDrawX < barAdvance);

	ImGui::EndFrame();
	ImGui::DestroyContext(context);
}

TEST_CASE("MeasureGlyphInk reads the ink from the alpha of an oversampled glyph's pixels, not from its box")
{
	ImGuiContext* context = ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(800.0f, 600.0f);
	io.IniFilename = nullptr;

	constexpr int atlasPixelsPerScreenPixel = 2;

	ImFontConfig oversampledFont;
	oversampledFont.OversampleH = atlasPixelsPerScreenPixel;
	io.Fonts->AddFontDefaultBitmap(&oversampledFont);
	io.Fonts->Build();
	ImGui::NewFrame();

	const ImFontGlyph* const glyph = ImGui::GetFontBaked()->FindGlyph('W');
	ImTextureData* const atlas = io.Fonts->TexData;
	REQUIRE(atlas->Format == ImTextureFormat_RGBA32);

	const int boxLeft = static_cast<int>(glyph->U0 * static_cast<float>(atlas->Width) + 0.5f);
	const int boxRight = static_cast<int>(glyph->U1 * static_cast<float>(atlas->Width) + 0.5f);
	const int boxTop = static_cast<int>(glyph->V0 * static_cast<float>(atlas->Height) + 0.5f);
	const int boxBottom = static_cast<int>(glyph->V1 * static_cast<float>(atlas->Height) + 0.5f);
	REQUIRE(boxRight - boxLeft > 4);

	constexpr int inkedColumnInBox = 3;
	constexpr int alphaByte = 3;
	const int inkedColumn = boxLeft + inkedColumnInBox;

	for (int row = boxTop; row < boxBottom; ++row)
	{
		for (int column = boxLeft; column < boxRight; ++column)
		{
			unsigned char* const pixel = static_cast<unsigned char*>(atlas->GetPixelsAt(column, row));

			for (int colourByte = 0; colourByte < alphaByte; ++colourByte)
			{
				pixel[colourByte] = 255;
			}

			pixel[alphaByte] = column == inkedColumn ? 255 : 0;
		}
	}

	const Core::GlyphInk ink = Core::MeasureGlyphInk("W");

	const float expectedStart =
		glyph->X0 + static_cast<float>(inkedColumnInBox) / static_cast<float>(atlasPixelsPerScreenPixel);
	const float expectedEnd =
		glyph->X0 + static_cast<float>(inkedColumnInBox + 1) / static_cast<float>(atlasPixelsPerScreenPixel);

	CHECK(ink.StartFromDrawX == doctest::Approx(expectedStart));
	CHECK(ink.EndFromDrawX == doctest::Approx(expectedEnd));

	ImGui::EndFrame();
	ImGui::DestroyContext(context);
}
