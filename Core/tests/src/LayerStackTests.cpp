#include <doctest/doctest.h>

#include "Core/App/LayerStack.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
    class RecordingLayer final : public core::Layer
    {
    public:
        RecordingLayer(std::string name, std::vector<std::string>& journal)
            : core::Layer(std::move(name)), m_journal(journal)
        {
        }

        void OnAttach() override { m_journal.push_back(Name() + ":attach"); }
        void OnDetach() override { m_journal.push_back(Name() + ":detach"); }
        void OnUpdate(float) override { m_journal.push_back(Name() + ":update"); }
        void OnUIRender() override { m_journal.push_back(Name() + ":ui"); }

    private:
        std::vector<std::string>& m_journal;
    };
}

TEST_CASE("Push wola OnAttach i zwraca referencje do warstwy")
{
    std::vector<std::string> journal;
    core::LayerStack stack;

    core::Layer& layer = stack.Push(std::make_unique<RecordingLayer>("pierwsza", journal));

    CHECK(stack.Size() == 1);
    CHECK(layer.Name() == "pierwsza");
    REQUIRE(journal.size() == 1);
    CHECK(journal[0] == "pierwsza:attach");
}

TEST_CASE("Iteracja zachowuje kolejnosc dodania")
{
    std::vector<std::string> journal;
    core::LayerStack stack;

    stack.Push(std::make_unique<RecordingLayer>("a", journal));
    stack.Push(std::make_unique<RecordingLayer>("b", journal));
    journal.clear();

    for (auto& layer : stack)
        layer->OnUpdate(0.016f);

    REQUIRE(journal.size() == 2);
    CHECK(journal[0] == "a:update");
    CHECK(journal[1] == "b:update");
}

TEST_CASE("Clear odlacza warstwy w kolejnosci odwrotnej do dodania")
{
    std::vector<std::string> journal;
    core::LayerStack stack;

    stack.Push(std::make_unique<RecordingLayer>("a", journal));
    stack.Push(std::make_unique<RecordingLayer>("b", journal));
    journal.clear();

    stack.Clear();

    REQUIRE(journal.size() == 2);
    CHECK(journal[0] == "b:detach");
    CHECK(journal[1] == "a:detach");
    CHECK(stack.Size() == 0);
}
