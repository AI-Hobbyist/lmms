#include "vsthost/RealtimeMidiQueue.h"
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

using lmms::vsthost::RealtimeMidiQueue;
int main()
{
	RealtimeMidiQueue queue;
	int failures = 0;
	for (unsigned i = 0; i < queue.Capacity; ++i)
	{
		if (!queue.push({7, {i, 0, 0, 0, 0}}))
		{
			++failures;
		}
	}
	if (queue.push({}))
	{
		++failures;
	}
	RealtimeMidiQueue::Event event{};
	for (unsigned i = 0; i < queue.Capacity; ++i)
	{
		if (!queue.pop(event) || event.generation != 7 || event.values[0] != i)
		{
			++failures;
		}
	}
	if (queue.pop(event))
	{
		++failures;
	}
	constexpr unsigned Producers = 3, Events = 10000;
	std::array<unsigned, Producers> next{};
	std::atomic<bool> cancel{false};
	std::vector<std::jthread> producers;
	for (unsigned producer = 0; producer < Producers; ++producer)
	{
		producers.emplace_back([&, producer] {
			for (unsigned index = 0; index < Events && !cancel.load(); ++index)
			{
				while (!queue.push({11, {producer, index, 0, 0, 0}}) && !cancel.load())
				{
					std::this_thread::yield();
				}
			}
		});
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	unsigned received = 0;
	while (received < Producers * Events && std::chrono::steady_clock::now() < deadline)
	{
		if (!queue.pop(event))
		{
			std::this_thread::yield();
			continue;
		}
		if (event.generation != 11 || event.values[0] >= Producers || event.values[1] != next[event.values[0]]++)
		{
			++failures;
		}
		++received;
	}
	cancel.store(true);
	producers.clear();
	if (received != Producers * Events || queue.pop(event))
	{
		++failures;
	}
	std::cout << "Concurrent MIDI queue: " << received << " events, " << failures << " failures\n";
	return failures ? 1 : 0;
}
