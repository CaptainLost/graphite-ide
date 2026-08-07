#include "corepch.h"

#include "Core/Base/IniFile.h"

#include <algorithm>
#include <fstream>

namespace core
{
    IniFile IniFile::Load(const std::filesystem::path& path)
    {
        IniFile file;

        std::ifstream stream{path};
        if (!stream.is_open())
            return file;

        std::string line;
        while (std::getline(stream, line))
        {
            // A file written on another machine can carry a CR that text mode does not
            // strip; it would otherwise become part of the value.
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            const std::size_t separator = line.find('=');
            if (separator == std::string::npos || separator == 0)
                continue;

            // Split on the FIRST '=' only, so a value may contain more of them.
            file.Set(line.substr(0, separator), line.substr(separator + 1));
        }

        return file;
    }

    std::string IniFile::Get(std::string_view key) const
    {
        const auto entry = std::find_if(m_entries.begin(), m_entries.end(),
                                        [key](const Entry& candidate)
                                        { return candidate.key == key; });

        return entry == m_entries.end() ? std::string{} : entry->value;
    }

    void IniFile::Set(std::string_view key, std::string value)
    {
        const auto entry = std::find_if(m_entries.begin(), m_entries.end(),
                                        [key](const Entry& candidate)
                                        { return candidate.key == key; });

        if (entry != m_entries.end())
        {
            entry->value = std::move(value);
            return;
        }

        m_entries.push_back(Entry{std::string{key}, std::move(value)});
    }

    bool IniFile::Save(const std::filesystem::path& path) const
    {
        std::ofstream stream{path, std::ios::trunc};
        if (!stream.is_open())
            return false;

        for (const Entry& entry : m_entries)
            stream << entry.key << '=' << entry.value << '\n';

        return stream.good();
    }
}
