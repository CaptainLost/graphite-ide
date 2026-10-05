#include "App/LogoShape.h"

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>

namespace Graphite {

	namespace {
		struct Gradient
		{
			ImVec2 From;
			ImVec2 To;
			ImU32 FromColour = 0;
			ImU32 ToColour = 0;

			// userSpaceOnUse: From and To are in the SVG's units. Otherwise they are fractions
			// of the filled face's bounds, which is SVG's default.
			bool UserSpace = false;
		};

		std::optional<ImU32> ParseColour(std::string_view text)
		{
			if (text.size() != 7 || text[0] != '#')
			{
				return std::nullopt;
			}

			unsigned int rgb = 0;
			const auto [end, error] = std::from_chars(text.data() + 1, text.data() + text.size(), rgb, 16);

			if (error != std::errc() || end != text.data() + text.size())
			{
				return std::nullopt;
			}

			return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF);
		}

		std::optional<Gradient> ReadGradient(const pugi::xml_node& node)
		{
			const pugi::xml_node first = node.child("stop");
			pugi::xml_node last = first;

			for (pugi::xml_node stop = first; stop; stop = stop.next_sibling("stop"))
			{
				last = stop;
			}

			if (!first)
			{
				return std::nullopt;
			}

			const auto fromColour = ParseColour(first.attribute("stop-color").as_string());
			const auto toColour = ParseColour(last.attribute("stop-color").as_string());

			if (!fromColour || !toColour)
			{
				return std::nullopt;
			}

			// SVG's defaults: left to right across the bounds.
			Gradient gradient;
			gradient.From = ImVec2(node.attribute("x1").as_float(0.0f), node.attribute("y1").as_float(0.0f));
			gradient.To = ImVec2(node.attribute("x2").as_float(1.0f), node.attribute("y2").as_float(0.0f));
			gradient.FromColour = *fromColour;
			gradient.ToColour = *toColour;
			gradient.UserSpace = std::string_view(node.attribute("gradientUnits").as_string()) == "userSpaceOnUse";
			return gradient;
		}

		// Absolute M, L and Z only, and a single outline: a second M would start a hole or a
		// second piece, which one concave fill cannot draw.
		std::optional<std::vector<ImVec2>> ParseOutline(std::string_view d)
		{
			std::vector<ImVec2> points;
			std::vector<float> numbers;
			char command = 0;
			bool closed = false;

			const auto flush = [&]() -> bool {
				if (numbers.size() % 2 != 0)
				{
					return false;
				}

				for (std::size_t i = 0; i < numbers.size(); i += 2)
				{
					points.emplace_back(numbers[i], numbers[i + 1]);
				}

				numbers.clear();
				return true;
			};

			std::size_t at = 0;

			while (at < d.size())
			{
				const char c = d[at];

				if (std::isspace(static_cast<unsigned char>(c)) || c == ',')
				{
					++at;
					continue;
				}

				if (std::isalpha(static_cast<unsigned char>(c)))
				{
					if (!flush())
					{
						return std::nullopt;
					}

					const bool repeatedMove = c == 'M' && command != 0;

					if ((c != 'M' && c != 'L' && c != 'Z') || repeatedMove || closed || (command == 0 && c != 'M'))
					{
						return std::nullopt;
					}

					command = c;
					closed = c == 'Z';
					++at;
					continue;
				}

				if (command == 0 || closed)
				{
					return std::nullopt;
				}

				float value = 0.0f;
				const auto [end, error] = std::from_chars(d.data() + at, d.data() + d.size(), value);

				if (error != std::errc())
				{
					return std::nullopt;
				}

				numbers.push_back(value);
				at = static_cast<std::size_t>(end - d.data());
			}

			if (!flush() || points.size() < 3)
			{
				return std::nullopt;
			}

			return points;
		}
	}

	LogoShape ParseLogoSvg(std::string_view svg)
	{
		pugi::xml_document document;

		if (!document.load_buffer(svg.data(), svg.size()))
		{
			return {};
		}

		const pugi::xml_node root = document.child("svg");

		std::unordered_map<std::string, Gradient> gradients;

		for (const pugi::xml_node& node : root.child("defs").children("linearGradient"))
		{
			auto gradient = ReadGradient(node);

			if (!gradient)
			{
				return {};
			}

			gradients.emplace(node.attribute("id").as_string(), *gradient);
		}

		LogoShape shape;

		for (const pugi::xml_node& node : root.children("path"))
		{
			auto outline = ParseOutline(node.attribute("d").as_string());

			if (!outline)
			{
				return {};
			}

			LogoFace face;
			face.Points = std::move(*outline);

			ImVec2 min = face.Points.front();
			ImVec2 max = face.Points.front();

			for (const ImVec2& point : face.Points)
			{
				min = ImVec2(std::min(min.x, point.x), std::min(min.y, point.y));
				max = ImVec2(std::max(max.x, point.x), std::max(max.y, point.y));
			}

			const std::string_view fill = node.attribute("fill").as_string();

			if (fill.starts_with("url(#") && fill.ends_with(")"))
			{
				const auto found = gradients.find(std::string(fill.substr(5, fill.size() - 6)));

				if (found == gradients.end())
				{
					return {};
				}

				const Gradient& gradient = found->second;
				const auto place = [&](ImVec2 at) {
					return gradient.UserSpace ? at
											  : ImVec2(min.x + at.x * (max.x - min.x), min.y + at.y * (max.y - min.y));
				};

				face.From = place(gradient.From);
				face.To = place(gradient.To);
				face.FromColour = gradient.FromColour;
				face.ToColour = gradient.ToColour;
			}
			else if (const auto colour = ParseColour(fill))
			{
				face.From = min;
				face.To = max;
				face.FromColour = *colour;
				face.ToColour = *colour;
			}
			else
			{
				return {};
			}

			if (shape.Faces.empty())
			{
				shape.Min = min;
				shape.Max = max;
			}
			else
			{
				shape.Min = ImVec2(std::min(shape.Min.x, min.x), std::min(shape.Min.y, min.y));
				shape.Max = ImVec2(std::max(shape.Max.x, max.x), std::max(shape.Max.y, max.y));
			}

			shape.Faces.push_back(std::move(face));
		}

		return shape;
	}
}
