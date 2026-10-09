#include <doctest/doctest.h>

#include <tree_sitter/api.h>

#include <cstdint>
#include <ostream>
#include <string_view>

extern "C" const TSLanguage* tree_sitter_c_sharp();

TEST_CASE("The C# grammar loads into the tree-sitter runtime it is built with")
{
	TSParser* parser = ts_parser_new();

	CHECK(ts_language_abi_version(tree_sitter_c_sharp()) == 15);
	REQUIRE(ts_parser_set_language(parser, tree_sitter_c_sharp()));

	constexpr std::string_view source = "class A { }";
	TSTree* tree = ts_parser_parse_string(parser, nullptr, source.data(), static_cast<std::uint32_t>(source.size()));
	REQUIRE(tree != nullptr);

	const TSNode root = ts_tree_root_node(tree);
	CHECK(std::string_view(ts_node_type(root)) == "compilation_unit");
	CHECK_FALSE(ts_node_has_error(root));

	ts_tree_delete(tree);
	ts_parser_delete(parser);
}
