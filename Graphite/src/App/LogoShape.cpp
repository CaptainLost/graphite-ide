#include "App/LogoShape.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <unordered_map>

namespace Graphite {

	namespace {
		constexpr std::size_t kHexColourLength = 7;
		constexpr int kHexadecimalBase = 16;
		constexpr unsigned int kByteMask = 0xFF;
		constexpr unsigned int kRedShift = 16;
		constexpr unsigned int kGreenShift = 8;
		constexpr unsigned int kOpaqueAlpha = 0xFF;

		constexpr float kDefaultGradientStartX = 0.0f;
		constexpr float kDefaultGradientStartY = 0.0f;
		constexpr float kDefaultGradientEndX = 1.0f;
		constexpr float kDefaultGradientEndY = 0.0f;

		constexpr char kNoCommand = 0;
		constexpr std::size_t kNumbersPerPoint = 2;
		constexpr std::size_t kMinimumOutlinePoints = 3;

		constexpr std::string_view kUserSpaceUnits = "userSpaceOnUse";
		constexpr std::string_view kUrlPrefix = "url(#";
		constexpr std::string_view kUrlSuffix = ")";

		struct Gradient
		{
			ImVec2 From;
			ImVec2 To;
			ImU32 FromColour = 0;
			ImU32 ToColour = 0;
			bool IsInUserSpaceUnits = false;
		};

		using GradientsById = std::unordered_map<std::string, Gradient>;

		std::optional<ImU32> ParseColour(std::string_view text)
		{
			if (text.size() != kHexColourLength || text[0] != '#')
			{
				return std::nullopt;
			}

			unsigned int rgb = 0;
			const char* digitsBegin = text.data() + 1;
			const char* digitsEnd = text.data() + text.size();
			const auto [parseEnd, parseError] = std::from_chars(digitsBegin, digitsEnd, rgb, kHexadecimalBase);

			if (parseError != std::errc() || parseEnd != digitsEnd)
			{
				return std::nullopt;
			}

			return IM_COL32((rgb >> kRedShift) & kByteMask, (rgb >> kGreenShift) & kByteMask, rgb & kByteMask, kOpaqueAlpha);
		}

		std::optional<Gradient> ReadGradient(const pugi::xml_node& node)
		{
			const pugi::xml_node firstStop = node.child("stop");
			pugi::xml_node lastStop = firstStop;

			for (pugi::xml_node stop = firstStop; stop; stop = stop.next_sibling("stop"))
			{
				lastStop = stop;
			}

			if (!firstStop)
			{
				return std::nullopt;
			}

			const std::optional<ImU32> fromColour = ParseColour(firstStop.attribute("stop-color").as_string());
			const std::optional<ImU32> toColour = ParseColour(lastStop.attribute("stop-color").as_string());

			if (!fromColour || !toColour)
			{
				return std::nullopt;
			}

			Gradient gradient;
			gradient.From = ImVec2(node.attribute("x1").as_float(kDefaultGradientStartX), node.attribute("y1").as_float(kDefaultGradientStartY));
			gradient.To = ImVec2(node.attribute("x2").as_float(kDefaultGradientEndX), node.attribute("y2").as_float(kDefaultGradientEndY));
			gradient.FromColour = *fromColour;
			gradient.ToColour = *toColour;
			gradient.IsInUserSpaceUnits = std::string_view(node.attribute("gradientUnits").as_string()) == kUserSpaceUnits;
			return gradient;
		}

		bool MoveNumbersToPoints(std::vector<float>& numbers, std::vector<ImVec2>& points)
		{
			if (numbers.size() % kNumbersPerPoint != 0)
			{
				return false;
			}

			for (std::size_t numberIndex = 0; numberIndex < numbers.size(); numberIndex += kNumbersPerPoint)
			{
				points.emplace_back(numbers[numberIndex], numbers[numberIndex + 1]);
			}

			numbers.clear();
			return true;
		}

		bool IsSupportedCommand(char command)
		{
			return command == 'M' || command == 'L' || command == 'Z';
		}

		std::optional<std::vector<ImVec2>> ParseSingleAbsoluteOutline(std::string_view pathData)
		{
			std::vector<ImVec2> points;
			std::vector<float> numbers;
			char command = kNoCommand;
			bool closed = false;
			std::size_t position = 0;

			while (position < pathData.size())
			{
				const char character = pathData[position];

				if (std::isspace(static_cast<unsigned char>(character)) || character == ',')
				{
					++position;
					continue;
				}

				if (std::isalpha(static_cast<unsigned char>(character)))
				{
					if (!MoveNumbersToPoints(numbers, points))
					{
						return std::nullopt;
					}

					const bool isFirstCommand = command == kNoCommand;
					const bool repeatedMove = character == 'M' && !isFirstCommand;
					const bool startsWithoutMove = isFirstCommand && character != 'M';

					if (!IsSupportedCommand(character) || repeatedMove || closed || startsWithoutMove)
					{
						return std::nullopt;
					}

					command = character;
					closed = character == 'Z';
					++position;
					continue;
				}

				if (command == kNoCommand || closed)
				{
					return std::nullopt;
				}

				float value = 0.0f;
				const auto [parseEnd, parseError] = std::from_chars(pathData.data() + position, pathData.data() + pathData.size(), value);

				if (parseError != std::errc())
				{
					return std::nullopt;
				}

				numbers.push_back(value);
				position = static_cast<std::size_t>(parseEnd - pathData.data());
			}

			if (!MoveNumbersToPoints(numbers, points) || points.size() < kMinimumOutlinePoints)
			{
				return std::nullopt;
			}

			return points;
		}

		ImVec2 GradientPointInFace(const Gradient& gradient, ImVec2 point, ImVec2 faceMin, ImVec2 faceMax)
		{
			if (gradient.IsInUserSpaceUnits)
			{
				return point;
			}

			return ImVec2(faceMin.x + point.x * (faceMax.x - faceMin.x), faceMin.y + point.y * (faceMax.y - faceMin.y));
		}
	}

	LogoShape ParseLogoSvg(std::string_view svg)
	{
		pugi::xml_document document;

		if (!document.load_buffer(svg.data(), svg.size()))
		{
			return LogoShape{};
		}

		const pugi::xml_node root = document.child("svg");

		GradientsById gradients;

		for (const pugi::xml_node& node : root.child("defs").children("linearGradient"))
		{
			const std::optional<Gradient> gradient = ReadGradient(node);

			if (!gradient)
			{
				return LogoShape{};
			}

			gradients.emplace(node.attribute("id").as_string(), *gradient);
		}

		LogoShape shape;

		for (const pugi::xml_node& node : root.children("path"))
		{
			std::optional<std::vector<ImVec2>> outline = ParseSingleAbsoluteOutline(node.attribute("d").as_string());

			if (!outline)
			{
				return LogoShape{};
			}

			LogoFace face;
			face.Points = std::move(*outline);

			ImVec2 faceMin = face.Points.front();
			ImVec2 faceMax = face.Points.front();

			for (const ImVec2& point : face.Points)
			{
				faceMin = ImVec2(std::min(faceMin.x, point.x), std::min(faceMin.y, point.y));
				faceMax = ImVec2(std::max(faceMax.x, point.x), std::max(faceMax.y, point.y));
			}

			const std::string_view fill = node.attribute("fill").as_string();

			if (fill.starts_with(kUrlPrefix) && fill.ends_with(kUrlSuffix))
			{
				const std::size_t gradientIdLength = fill.size() - kUrlPrefix.size() - kUrlSuffix.size();
				const GradientsById::const_iterator found = gradients.find(std::string(fill.substr(kUrlPrefix.size(), gradientIdLength)));

				if (found == gradients.end())
				{
					return LogoShape{};
				}

				[[maybe_unused]] const auto& [gradientId, gradient] = *found;

				face.From = GradientPointInFace(gradient, gradient.From, faceMin, faceMax);
				face.To = GradientPointInFace(gradient, gradient.To, faceMin, faceMax);
				face.FromColour = gradient.FromColour;
				face.ToColour = gradient.ToColour;
			}
			else if (const std::optional<ImU32> flatColour = ParseColour(fill))
			{
				face.From = faceMin;
				face.To = faceMax;
				face.FromColour = *flatColour;
				face.ToColour = *flatColour;
			}
			else
			{
				return LogoShape{};
			}

			if (shape.Faces.empty())
			{
				shape.Min = faceMin;
				shape.Max = faceMax;
			}
			else
			{
				shape.Min = ImVec2(std::min(shape.Min.x, faceMin.x), std::min(shape.Min.y, faceMin.y));
				shape.Max = ImVec2(std::max(shape.Max.x, faceMax.x), std::max(shape.Max.y, faceMax.y));
			}

			shape.Faces.push_back(std::move(face));
		}

		return shape;
	}
}
