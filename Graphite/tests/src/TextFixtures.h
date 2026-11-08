#pragma once

#include "Core/FileSystem/FileSystem.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>

// The environment variable `name`, or empty when it is not set: what a gated acceptance test is given.
inline std::string EnvironmentText(const char* name)
{
	char* value = nullptr;
	std::size_t length = 0;

	if (::_dupenv_s(&value, &length, name) != 0 || value == nullptr)
	{
		return {};
	}

	std::string text{value};
	std::free(value);
	return text;
}

// The path in UTF-8 with forward slashes, as a file:/// URL names it.
inline std::string ForwardSlashes(const std::filesystem::path& path)
{
	std::string text = Core::ToUtf8(path);

	for (char& character : text)
	{
		if (character == '\\')
		{
			character = '/';
		}
	}

	return text;
}
