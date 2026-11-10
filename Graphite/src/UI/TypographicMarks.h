#pragma once

#include <string_view>

namespace Graphite {

	inline constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

	inline constexpr std::string_view kBullet = "\xE2\x80\xA2";

	inline constexpr std::string_view kBulletSeparator = " \xE2\x80\xA2 ";

	inline constexpr std::string_view kMiddleDotSeparator = " \xC2\xB7 ";

	inline constexpr std::string_view kEmDashSeparator = " \xE2\x80\x94 ";

	inline constexpr std::string_view kEnDash = "\xE2\x80\x93";

	inline constexpr std::string_view kMinusSign = "\xE2\x88\x92";

	inline constexpr std::string_view kMultiplicationSign = "\xC3\x97";

	inline constexpr std::string_view kRightwardsArrow = "\xE2\x86\x92";

	inline constexpr std::string_view kBreadcrumbSeparator = " \xE2\x80\xBA ";

	inline constexpr std::string_view kLeftDoubleQuotationMark = "\xE2\x80\x9C";
	inline constexpr std::string_view kRightDoubleQuotationMark = "\xE2\x80\x9D";
}
