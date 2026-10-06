#include <doctest/doctest.h>

#include "Core/App/LayerStack.h"
#include "Core/Events/KeyEvent.h"

#include <memory>
#include <string>
#include <vector>

namespace {

	constexpr Core::Timestep kOneFrameAtSixtyHertz{0.016f};
	constexpr bool kFirstPress = false;

	class RecordingLayer final : public Core::Layer
	{
	public:
		RecordingLayer(std::string name, std::vector<std::string>& journal)
			: Core::Layer(std::move(name)), m_Journal(journal)
		{
		}

		void OnAttach() override { m_Journal.push_back(GetName() + ":attach"); }
		void OnDetach() override { m_Journal.push_back(GetName() + ":detach"); }
		void OnUpdate(Core::Timestep) override { m_Journal.push_back(GetName() + ":update"); }
		void OnImGuiRender() override { m_Journal.push_back(GetName() + ":ui"); }
		void OnEvent(Core::Event&) override { m_Journal.push_back(GetName() + ":event"); }

	private:
		std::vector<std::string>& m_Journal;
	};

	class ConsumingLayer final : public Core::Layer
	{
	public:
		ConsumingLayer(std::string name, std::vector<std::string>& journal)
			: Core::Layer(std::move(name)), m_Journal(journal)
		{
		}

		void OnEvent(Core::Event& event) override
		{
			m_Journal.push_back(GetName() + ":event");
			event.Handled = true;
		}

	private:
		std::vector<std::string>& m_Journal;
	};
}

TEST_CASE("Push calls OnAttach and returns a reference to the layer")
{
	std::vector<std::string> journal;
	Core::LayerStack stack;

	Core::Layer& layer = stack.Push(std::make_unique<RecordingLayer>("first", journal));

	CHECK(stack.GetSize() == 1);
	CHECK(layer.GetName() == "first");
	REQUIRE(journal.size() == 1);
	CHECK(journal[0] == "first:attach");
}

TEST_CASE("Iteration preserves insertion order")
{
	std::vector<std::string> journal;
	Core::LayerStack stack;

	stack.Push(std::make_unique<RecordingLayer>("a", journal));
	stack.Push(std::make_unique<RecordingLayer>("b", journal));
	journal.clear();

	for (Core::Scope<Core::Layer>& layer : stack)
	{
		layer->OnUpdate(kOneFrameAtSixtyHertz);
	}

	REQUIRE(journal.size() == 2);
	CHECK(journal[0] == "a:update");
	CHECK(journal[1] == "b:update");
}

TEST_CASE("Reverse iteration walks the layers from the top down with the most recently pushed first")
{
	std::vector<std::string> journal;
	Core::LayerStack stack;

	stack.Push(std::make_unique<RecordingLayer>("bottom", journal));
	stack.Push(std::make_unique<RecordingLayer>("top", journal));
	journal.clear();

	for (Core::LayerStack::Container::reverse_iterator layerFromTheTop = stack.rbegin(); layerFromTheTop != stack.rend(); ++layerFromTheTop)
	{
		(*layerFromTheTop)->OnUpdate(kOneFrameAtSixtyHertz);
	}

	REQUIRE(journal.size() == 2);
	CHECK(journal[0] == "top:update");
	CHECK(journal[1] == "bottom:update");
}

TEST_CASE("Clear detaches layers in reverse order of addition so a later layer may depend on an earlier one")
{
	std::vector<std::string> journal;
	Core::LayerStack stack;

	stack.Push(std::make_unique<RecordingLayer>("a", journal));
	stack.Push(std::make_unique<RecordingLayer>("b", journal));
	journal.clear();

	stack.Clear();

	REQUIRE(journal.size() == 2);
	CHECK(journal[0] == "b:detach");
	CHECK(journal[1] == "a:detach");
	CHECK(stack.GetSize() == 0);
}

TEST_CASE("Adding a layer during iteration over the copy does not invalidate the pass")
{
	std::vector<std::string> journal;
	Core::LayerStack stack;

	stack.Push(std::make_unique<RecordingLayer>("a", journal));
	stack.Push(std::make_unique<RecordingLayer>("b", journal));
	journal.clear();

	std::vector<Core::Layer*> snapshot;
	for (Core::Scope<Core::Layer>& layer : stack)
	{
		snapshot.push_back(layer.get());
	}

	for (Core::Layer* layer : snapshot)
	{
		layer->OnUpdate(kOneFrameAtSixtyHertz);
		stack.Push(std::make_unique<RecordingLayer>("added", journal));
	}

	CHECK(stack.GetSize() == 4);
	REQUIRE(journal.size() == 4);
	CHECK(journal == std::vector<std::string>{"a:update", "added:attach", "b:update", "added:attach"});
}

TEST_CASE("An event is offered to every layer from the top down while none handles it")
{
	std::vector<std::string> journal;
	Core::LayerStack stack;

	stack.Push(std::make_unique<RecordingLayer>("bottom", journal));
	stack.Push(std::make_unique<RecordingLayer>("top", journal));
	journal.clear();

	Core::KeyPressedEvent event{Core::KeyCode::A, Core::KeyModifier::None, kFirstPress};
	stack.OfferEventFromTheTopUntilHandled(event);

	CHECK(journal == std::vector<std::string>{"top:event", "bottom:event"});
}

TEST_CASE("A layer that handles an event stops it reaching the layers below so the topmost interested layer wins")
{
	std::vector<std::string> journal;
	Core::LayerStack stack;

	stack.Push(std::make_unique<RecordingLayer>("bottom", journal));
	stack.Push(std::make_unique<ConsumingLayer>("top", journal));
	journal.clear();

	Core::KeyPressedEvent event{Core::KeyCode::A, Core::KeyModifier::None, kFirstPress};
	stack.OfferEventFromTheTopUntilHandled(event);

	CHECK(journal == std::vector<std::string>{"top:event"});
	CHECK(event.Handled);
}

TEST_CASE("An event the GUI already handled reaches no layer")
{
	std::vector<std::string> journal;
	Core::LayerStack stack;

	stack.Push(std::make_unique<RecordingLayer>("bottom", journal));
	stack.Push(std::make_unique<RecordingLayer>("top", journal));
	journal.clear();

	Core::KeyPressedEvent event{Core::KeyCode::A, Core::KeyModifier::None, kFirstPress};
	event.Handled = true;
	stack.OfferEventFromTheTopUntilHandled(event);

	CHECK(journal.empty());
}
