#include <doctest/doctest.h>

#include "Core/Git/Lines/AncestryGraph.h"

#include "Git/Lines/GraphBuilder.h"

TEST_CASE("Lines: a graph indexes commits and resolves parents appended later in the same batch")
{
	GraphBuilder b;
	b.Commit("child", {"root"}, 2);
	b.Commit("root", {}, 1);
	const Core::AncestryGraph graph = b.Graph();
	REQUIRE(graph.Size() == 2);
	const auto child = graph.Find(b.Id("child"));
	const auto root = graph.Find(b.Id("root"));
	REQUIRE(graph.ParentsOf(child).size() == 1);
	CHECK(graph.ParentsOf(child)[0] == root);
	CHECK(graph.ParentsOf(root).empty());
	CHECK_FALSE(graph.ParentsCutAt(child));
	CHECK(graph.TimeOf(child) == GraphBuilder::Seconds(2));
	CHECK(graph.IdOf(root) == b.Id("root"));
	CHECK(graph.Find(OidOf("absent")) == Core::AncestryGraph::kNoCommit);
}

TEST_CASE("Lines: a parent the graph never receives is dropped and its child is marked cut")
{
	Core::AncestryGraph graph;
	graph.Append({Core::GraphCommit{OidOf("a"), {OidOf("missing")}, 10, "a@example.com", false}});
	CHECK(graph.ParentsOf(graph.Find(OidOf("a"))).empty());
	CHECK(graph.ParentsCutAt(graph.Find(OidOf("a"))));
}

TEST_CASE("Lines: a commit whose first parent is missing loses every parent and is marked cut")
{
	Core::AncestryGraph graph;
	graph.Append({Core::GraphCommit{OidOf("side"), {}, 5, "a@example.com", false},
				  Core::GraphCommit{OidOf("merge"), {OidOf("missing"), OidOf("side")}, 10, "a@example.com", false},
				  Core::GraphCommit{OidOf("other"), {OidOf("side"), OidOf("missing")}, 11, "a@example.com", false}});

	const auto merge = graph.Find(OidOf("merge"));
	CHECK(graph.ParentsOf(merge).empty());
	CHECK(graph.ParentsCutAt(merge));

	// A missing second parent leaves the first parent where it is.
	const auto other = graph.Find(OidOf("other"));
	REQUIRE(graph.ParentsOf(other).size() == 1);
	CHECK(graph.ParentsOf(other)[0] == graph.Find(OidOf("side")));
	CHECK(graph.ParentsCutAt(other));
}

TEST_CASE("Lines: appending a commit twice keeps one and authors are shared by key")
{
	GraphBuilder b;
	b.Commit("one", {}, 1, "x@example.com");
	b.Commit("two", {"one"}, 2, "x@example.com");
	b.Commit("one", {}, 1, "x@example.com");
	b.Commit("three", {"two", "one"}, 3, "y@example.com");
	const Core::AncestryGraph graph = b.Graph();

	CHECK(graph.Size() == 3);
	CHECK(graph.AuthorKeys().size() == 2);
	CHECK(graph.AuthorOf(graph.Find(b.Id("one"))) == graph.AuthorOf(graph.Find(b.Id("two"))));
	CHECK(graph.AuthorOf(graph.Find(b.Id("three"))) != graph.AuthorOf(graph.Find(b.Id("one"))));
	CHECK(graph.ParentsOf(graph.Find(b.Id("three"))).size() == 2);

	Core::AncestryGraph copy;
	copy.Append(graph.ToCommits());
	CHECK(copy.Size() == 3);
	CHECK(copy.ToCommits() == graph.ToCommits());
}

TEST_CASE("Lines: a second batch links to commits of the first and a cut flag survives the round trip")
{
	GraphBuilder b;
	b.Commit("old", {"gone"}, 1);
	b.Cut("old");
	Core::AncestryGraph graph = b.Graph();
	graph.Append({Core::GraphCommit{OidOf("new"), {OidOf("old")}, 5, "a@example.com", false}});

	const auto fresh = graph.Find(OidOf("new"));
	REQUIRE(graph.ParentsOf(fresh).size() == 1);
	CHECK(graph.ParentsOf(fresh)[0] == graph.Find(OidOf("old")));
	CHECK(graph.ParentsCutAt(graph.Find(OidOf("old"))));

	Core::AncestryGraph copy;
	copy.Append(graph.ToCommits());
	CHECK(copy.ToCommits() == graph.ToCommits());
}

TEST_CASE("Lines: a chain names its commits in order and takes authors in turn")
{
	GraphBuilder b;
	b.Chain("c", 3, std::nullopt, 0, {"p@example.com", "q@example.com"});
	const Core::AncestryGraph graph = b.Graph();

	REQUIRE(graph.Size() == 3);
	CHECK(graph.ParentsOf(graph.Find(b.Id("c1"))).empty());
	CHECK(graph.ParentsOf(graph.Find(b.Id("c3")))[0] == graph.Find(b.Id("c2")));
	CHECK(graph.AuthorKeys().size() == 2);
	CHECK(b.Now() == GraphBuilder::Seconds(100));
	CHECK(GraphBuilder::Seconds(0) == 1767225600);
}

TEST_CASE("Lines: who committed a commit survives a second batch and the round trip")
{
	GraphBuilder b;
	b.Commit("own", {}, 1, "p@example.com");
	b.Commit("landed", {"own"}, 2, "p@example.com", GraphBuilder::kService);
	Core::AncestryGraph graph = b.Graph();
	graph.Append({Core::GraphCommit{OidOf("picked"), {OidOf("landed")}, 3, "q@example.com", false, false}});

	CHECK(graph.CommittedByAuthor(graph.Find(b.Id("own"))));
	CHECK_FALSE(graph.CommittedByAuthor(graph.Find(b.Id("landed"))));
	CHECK_FALSE(graph.CommittedByAuthor(graph.Find(OidOf("picked"))));

	Core::AncestryGraph copy;
	copy.Append(graph.ToCommits());
	CHECK(copy.ToCommits() == graph.ToCommits());
	CHECK(copy.CommittedByAuthor(copy.Find(b.Id("own"))));
	CHECK_FALSE(copy.CommittedByAuthor(copy.Find(b.Id("landed"))));
	CHECK_FALSE(copy.CommittedByAuthor(copy.Find(OidOf("picked"))));
}
