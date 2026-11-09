#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <vector>

namespace Graphite {

	// A place the caret was, by file, line and byte column.
	struct NavigationPoint
	{
		std::filesystem::path Path;
		std::size_t Line = 0;
		std::size_t Column = 0;

		friend bool operator==(const NavigationPoint&, const NavigationPoint&) = default;
	};

	// Back and Forward over the jumps made: F12, a Ctrl+click, Go to Line, Go to File. Each jump notes where it left
	// from; Back returns there and Forward undoes the Back. A new jump forgets what Forward could reach, as a browser does.
	class NavigationHistory
	{
	public:
		static constexpr std::size_t kLongestHistory = 50;

		void NoteDeparture(NavigationPoint from);

		// Where Back goes from `current`, which Forward then returns to; nothing when there is no earlier place.
		std::optional<NavigationPoint> Back(NavigationPoint current);
		std::optional<NavigationPoint> Forward(NavigationPoint current);

		bool CanGoBack() const { return !m_Back.empty(); }
		bool CanGoForward() const { return !m_Forward.empty(); }

	private:
		std::vector<NavigationPoint> m_Back;
		std::vector<NavigationPoint> m_Forward;
	};
}
