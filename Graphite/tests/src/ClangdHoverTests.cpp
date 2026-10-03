#include <doctest/doctest.h>

#include "Language/Server/ClangdHover.h"

using Graphite::HoverInfo;
using Graphite::ParseClangdHover;

namespace {
	// Real clangd 22 replies, captured from the spec's sample file.
	constexpr const char* kInstanceMethod = R"(### instance-method `OnUpdate`

---
→ `void`

Parameters:

- `Timestep ts`

Updates the layer.

---
```cpp
// In Layer
public: virtual void OnUpdate(Timestep ts)
```)";

	constexpr const char* kStaticMethod = R"(### static-method `Resize`

---
→ `bool`

Parameters:

- `unsigned int width`
- `unsigned int height`
- `bool force = false`

---
```cpp
// In Layer
public: static bool Resize(unsigned int width, unsigned int height, bool force = false)
```)";

	constexpr const char* kAutoVariable = R"(### variable `it`

---
Type: `iterator (aka _Vector_iterator<_Vector_val<std::_Simple_types<Core::Layer *>>>)`

---
```cpp
// In Run
auto it = layers.begin()
```)";

	constexpr const char* kConstant = R"(### variable `kMaxLayers`

---
Type: `const std::size_t (aka const unsigned long long)`

Value = `16 (0x10)`

---
```cpp
// In namespace Core
constexpr std::size_t kMaxLayers = 16
```)";

	constexpr const char* kEnumerator = R"(### enumerator `Slow`

---
Type: `enum Core::Mode`

Value = `4`

---
```cpp
// In Mode
Slow
```)";

	constexpr const char* kPaddedField = R"(### field `m_Minimized`

---
Type: `bool`

Offset: 8 bytes

Size: 1 byte (+7 bytes padding), alignment 1 byte

---
```cpp
// In Layer
private: bool m_Minimized = false
```)";

	constexpr const char* kField = R"(### field `m_Scale`

---
Type: `double`

Offset: 16 bytes

Size: 8 bytes, alignment 8 bytes

---
```cpp
// In Layer
private: double m_Scale = 1.0
```)";

	constexpr const char* kClass = R"(### class `Layer`

---
Size: 24 bytes, alignment 8 bytes

Called once per frame with the time since the previous one.

---
```cpp
// In namespace Core
class Layer {}
```)";

	constexpr const char* kParam = R"(### param `ts`

---
Type: `Timestep`

---
```cpp
// In Layer::OnUpdate
Timestep ts
```)";

	constexpr const char* kStep = R"(### function `Step`

---
→ `int`

Parameters:

- `int amount`

Advances the simulation by one step and
reports whether anything changed.

Type: this line looks like a field but is prose.  
\- a bullet in the docs  
\- another bullet

Example:  
@code
int x = Step(1);  
@endcode

---
```cpp
int Step(int amount)
```)";

	constexpr const char* kMakeMany = R"(### function `MakeMany`

---
→ `std::vector<int, std::allocator<int>>`

Parameters:

- `const int & value`
- `std::size_t count (aka unsigned long long)`
- `bool reserveFirst = true`

---
```cpp
template <>
std::vector<int, std::allocator<int>>
MakeMany<int, std::allocator<int>>(const int &value, std::size_t count,
                                   bool reserveFirst = true)
```)";

	constexpr const char* kTick = R"(### function `Tick`

---
→ `void`

Plain function, no parameters.

---
```cpp
void Tick()
```)";

	constexpr const char* kTimestep = R"(### struct `Timestep`

---
Size: 4 bytes, alignment 4 bytes

---
```cpp
// In namespace Core
struct Timestep {}
```)";

	constexpr const char* kInclude = R"(### `vector`

---
```
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\include\vector
```

---
provides `vector`)";
}

TEST_CASE("An instance method gives kind, name, scope, access, qualifiers, return, parameters, doc")
{
	const HoverInfo info = ParseClangdHover(kInstanceMethod);

	REQUIRE(info.Parsed);
	CHECK(info.Kind == "instance method");
	CHECK(info.Name == "OnUpdate");
	CHECK(info.Scope == "Layer");
	CHECK(info.Access == "public");
	CHECK(info.Qualifiers == std::vector<std::string>{"virtual"});
	CHECK(info.Declaration == "virtual void OnUpdate(Timestep ts)");
	CHECK(info.ReturnType == "void");
	CHECK(info.Parameters == std::vector<std::string>{"Timestep ts"});
	CHECK(info.Documentation == "Updates the layer.");
}

TEST_CASE("A static method keeps every parameter and its default argument")
{
	const HoverInfo info = ParseClangdHover(kStaticMethod);

	CHECK(info.Kind == "static method");
	CHECK(info.Qualifiers == std::vector<std::string>{"static"});
	CHECK(info.Parameters == std::vector<std::string>{"unsigned int width", "unsigned int height",
													  "bool force = false"});
	CHECK(info.Documentation.empty());
}

TEST_CASE("An auto variable splits its type from the aka")
{
	const HoverInfo info = ParseClangdHover(kAutoVariable);

	CHECK(info.Kind == "variable");
	CHECK(info.Scope == "Run");
	CHECK(info.Type == "iterator");
	CHECK(info.TypeAka == "_Vector_iterator<_Vector_val<std::_Simple_types<Core::Layer *>>>");
	CHECK(info.Access.empty());
	CHECK(info.Declaration == "auto it = layers.begin()");
}

TEST_CASE("A constant gives its value")
{
	const HoverInfo info = ParseClangdHover(kConstant);

	CHECK(info.Type == "const std::size_t");
	CHECK(info.TypeAka == "const unsigned long long");
	CHECK(info.Value == "16 (0x10)");
	CHECK(info.Scope == "namespace Core");
}

TEST_CASE("An enumerator gives its type and value")
{
	const HoverInfo info = ParseClangdHover(kEnumerator);

	CHECK(info.Kind == "enumerator");
	CHECK(info.Type == "enum Core::Mode");
	CHECK(info.Value == "4");
	CHECK(info.TypeAka.empty());
}

TEST_CASE("A field with padding gives offset, size, padding and alignment")
{
	const HoverInfo info = ParseClangdHover(kPaddedField);

	CHECK(info.Access == "private");
	CHECK(info.Offset == "8 bytes");
	CHECK(info.Size == "1 byte");
	CHECK(info.Padding == "7 bytes");
	CHECK(info.Alignment == "1 byte");
	CHECK(info.Declaration == "bool m_Minimized = false");
}

TEST_CASE("A field without padding leaves padding empty")
{
	const HoverInfo info = ParseClangdHover(kField);

	CHECK(info.Size == "8 bytes");
	CHECK(info.Padding.empty());
	CHECK(info.Alignment == "8 bytes");
}

TEST_CASE("A class gives its size and its doc comment")
{
	const HoverInfo info = ParseClangdHover(kClass);

	CHECK(info.Kind == "class");
	CHECK(info.Size == "24 bytes");
	CHECK(info.Alignment == "8 bytes");
	CHECK(info.Documentation == "Called once per frame with the time since the previous one.");
}

TEST_CASE("A parameter's scope is its function")
{
	const HoverInfo info = ParseClangdHover(kParam);

	CHECK(info.Kind == "param");
	CHECK(info.Scope == "Layer::OnUpdate");
	CHECK(info.Type == "Timestep");
}

// An include hover has no kind word and an unlabelled code block. Nothing must be lost: what
// cannot be placed lands in Documentation.
TEST_CASE("An include hover keeps its name and its text")
{
	const HoverInfo info = ParseClangdHover(kInclude);

	REQUIRE(info.Parsed);
	CHECK(info.Kind.empty());
	CHECK(info.Name == "vector");
	CHECK(info.Documentation.find("provides `vector`") != std::string::npos);
	CHECK(info.Raw == kInclude);
}

TEST_CASE("Empty and unrecognised replies are not parsed but keep the raw text")
{
	CHECK_FALSE(ParseClangdHover("").Parsed);

	const HoverInfo unknown = ParseClangdHover("some plaintext from another server");
	CHECK_FALSE(unknown.Parsed);
	CHECK(unknown.Raw == "some plaintext from another server");
}

TEST_CASE("A wrapped doc comment stays one paragraph and prose cannot overwrite the fields")
{
	const HoverInfo info = ParseClangdHover(kStep);

	CHECK(info.Documentation ==
		  "Advances the simulation by one step and\nreports whether anything changed.\n\n"
		  "Type: this line looks like a field but is prose.\n"
		  "- a bullet in the docs\n- another bullet\n\n"
		  "Example:\n@code\nint x = Step(1);\n@endcode");
	CHECK(info.Type.empty());
	CHECK(info.ReturnType == "int");
	CHECK(info.Parameters == std::vector<std::string>{"int amount"});
}

TEST_CASE("A declaration wrapped over several lines is read as one")
{
	const HoverInfo info = ParseClangdHover(kMakeMany);

	CHECK(info.Declaration ==
		  "template <> std::vector<int, std::allocator<int>> "
		  "MakeMany<int, std::allocator<int>>(const int &value, "
		  "std::size_t count, bool reserveFirst = true)");
	CHECK(info.Parameters == std::vector<std::string>{"const int & value",
													  "std::size_t count (aka unsigned long long)",
													  "bool reserveFirst = true"});
	CHECK(info.ReturnType == "std::vector<int, std::allocator<int>>");
}

TEST_CASE("A function without parameters has none and keeps its doc")
{
	const HoverInfo info = ParseClangdHover(kTick);

	CHECK(info.Parameters.empty());
	CHECK(info.Documentation == "Plain function, no parameters.");
}

TEST_CASE("A struct gives its size and alignment")
{
	const HoverInfo info = ParseClangdHover(kTimestep);

	CHECK(info.Kind == "struct");
	CHECK(info.Size == "4 bytes");
	CHECK(info.Alignment == "4 bytes");
}

TEST_CASE("CRLF line endings give the same fields as LF")
{
	std::string crlf;

	for (const char c : std::string(kField))
	{
		if (c == '\n')
		{
			crlf += '\r';
		}

		crlf += c;
	}

	const HoverInfo info = ParseClangdHover(crlf);

	CHECK(info.Kind == "field");
	CHECK(info.Name == "m_Scale");
	CHECK(info.Type == "double");
	CHECK(info.Offset == "16 bytes");
	CHECK(info.Size == "8 bytes");
	CHECK(info.Alignment == "8 bytes");
	CHECK(info.Access == "private");
	CHECK(info.Declaration == "double m_Scale = 1.0");
}
