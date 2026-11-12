#include "utils/sync_queue.h"
#include <atomic>
#include <cassert>
#include <future>
#include <iostream>
#include <thread>
using namespace std::chrono_literals;

int main() {
    {
        SyncQueue q;
        bool called = false;
        assert(!q.Run([&] { called = true; }, 1));
        q.Drain();
        assert(!called); // timed-out work must never execute later
    }
    {
        SyncQueue q;
        std::promise<void> entered, release;
        auto released = release.get_future().share();
        auto started = entered.get_future().share();
        auto result = std::async(std::launch::async, [&] {
            int stackValue = 42;
            return q.Run([&] {
                entered.set_value();
                released.wait();
                assert(stackValue == 42);
            }, 30);
        });
        std::thread pump([&] {
            while (started.wait_for(0ms) == std::future_status::timeout) {
                q.Drain();
            }
        });
        started.wait();
        std::this_thread::sleep_for(100ms);
        assert(result.wait_for(0ms) == std::future_status::timeout);
        release.set_value();
        pump.join();
        assert(result.get());
    }
    {
        SyncQueue q;
        auto result = std::async(std::launch::async, [&] {
            return q.Run([&] {
                assert(q.Run([] {}, 1)); // nested game callback never queues itself
                throw 1;
            }, 2000);
        });
        while (result.wait_for(0ms) != std::future_status::ready) {
            q.Drain();
            std::this_thread::yield();
        }
        assert(!result.get());
    }
    {
        SyncQueue q;
        bool called = false;
        auto result = std::async(std::launch::async, [&] {
            return q.Run([&] { called = true; }, 2000);
        });
        q.Stop();
        assert(!result.get());
        q.Drain();
        assert(!called);
        assert(!q.Run([] {}, 1));
    }
    std::cout << "PASS: cancellation, in-flight timeout lifetime, nested jobs, exceptions, shutdown\n";
}
