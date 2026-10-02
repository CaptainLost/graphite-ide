#include <doctest/doctest.h>

#include "Lang/Server/ClangdHover.h"

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
