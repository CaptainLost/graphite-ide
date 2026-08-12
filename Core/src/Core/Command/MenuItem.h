#pragma once

#include <string>
#include <vector>

namespace Core {

	class MenuItem
	{
	public:
		enum class Kind
		{
			Command,
			Separator,
			Submenu,
			Group
		};

		static MenuItem Command(std::string id);
		static MenuItem Separator();
		static MenuItem Submenu(std::string label, std::vector<MenuItem> children);

		static MenuItem Group(std::string group);

		Kind GetKind() const { return m_Kind; }
		const std::string& GetText() const { return m_Text; }
		const std::vector<MenuItem>& GetChildren() const { return m_Children; }

	private:
		MenuItem() = default;

		Kind m_Kind = Kind::Separator;

		std::string m_Text;
		std::vector<MenuItem> m_Children;
	};
}