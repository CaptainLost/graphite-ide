#pragma once

#include <optional>
#include <string>

#include <windows.h>

// Sets one of this process's variables, or removes it when given no value, and puts back what was there before on every
// path out of a test, a failed REQUIRE included.
class ScopedEnvironmentVariable
{
public:
	ScopedEnvironmentVariable(const wchar_t* name, const std::optional<std::wstring>& value)
		: m_Name(name), m_Previous(ReadVariable(name))
	{
		const wchar_t* const valueOrRemoval = value.has_value() ? value->c_str() : nullptr;

		m_Set = ::SetEnvironmentVariableW(m_Name.c_str(), valueOrRemoval) != FALSE;
	}

	~ScopedEnvironmentVariable()
	{
		const wchar_t* const previousOrRemoval = m_Previous.has_value() ? m_Previous->c_str() : nullptr;

		::SetEnvironmentVariableW(m_Name.c_str(), previousOrRemoval);
	}

	ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
	ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

	bool WasSet() const { return m_Set; }

private:
	// GetEnvironmentVariableW counts the terminator when it reports the length and leaves it out when it copies the value.
	static std::optional<std::wstring> ReadVariable(const wchar_t* name)
	{
		const DWORD measureOnly = 0;
		const DWORD lengthWithTerminator = ::GetEnvironmentVariableW(name, nullptr, measureOnly);

		if (lengthWithTerminator == 0)
		{
			return std::nullopt;
		}

		std::wstring value(lengthWithTerminator, L'\0');
		const DWORD charactersWritten = ::GetEnvironmentVariableW(name, value.data(), lengthWithTerminator);

		value.resize(charactersWritten);
		return value;
	}

	std::wstring m_Name;
	std::optional<std::wstring> m_Previous;
	bool m_Set = false;
};
