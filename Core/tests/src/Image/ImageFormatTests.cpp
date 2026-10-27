#include <doctest/doctest.h>

#include "Core/Image/ImageFormat.h"

using namespace Core;

TEST_CASE("Image files are recognised by their extension, whatever its case")
{
	CHECK(ImageFormatOfPath("logo.PNG") == ImageFormat::Png);
	CHECK(ImageFormatOfPath("photo.jpeg") == ImageFormat::Jpeg);
	CHECK(ImageFormatOfPath("photo.jpg") == ImageFormat::Jpeg);
	CHECK(ImageFormatOfPath("anim.Gif") == ImageFormat::Gif);
	CHECK(ImageFormatOfPath("scan.bmp") == ImageFormat::Bmp);
	CHECK(ImageFormatOfPath("app.ico") == ImageFormat::Ico);
	CHECK_FALSE(ImageFormatOfPath("picture.webp").has_value());
	CHECK_FALSE(ImageFormatOfPath("logo.svg").has_value());
	CHECK_FALSE(ImageFormatOfPath("png").has_value());
}

TEST_CASE("Image formats have a display name")
{
	CHECK(ImageFormatName(ImageFormat::Png) == "PNG");
	CHECK(ImageFormatName(ImageFormat::Jpeg) == "JPEG");
	CHECK(ImageFormatName(ImageFormat::Gif) == "GIF");
	CHECK(ImageFormatName(ImageFormat::Bmp) == "BMP");
	CHECK(ImageFormatName(ImageFormat::Ico) == "ICO");
}
