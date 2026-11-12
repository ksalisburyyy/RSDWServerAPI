#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

// A timeout cancels pending work only. Once work starts, its caller must stay
// alive until completion because existing engine callers capture stack references.
class SyncQueue {
    struct Job {
        std::function<void()> work;
        bool started = false;
        bool done = false;
        bool succeeded = false;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::shared_ptr<Job>> jobs;
    bool stopped = false;
    inline static thread_local SyncQueue* executing = nullptr;

public:
    bool Run(const std::function<void()>& work, int timeoutMs) {
        if (executing == this) {
            try { work(); return true; } catch (...) { return false; }
        }
        auto job = std::make_shared<Job>();
        job->work = work;
        std::unique_lock<std::mutex> lock(mutex);
        if (stopped || jobs.size() >= 128) return false;
        jobs.push_back(job);
        if (!changed.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                              [&] { return job->done; })) {
            if (!job->started) {
                auto it = std::find(jobs.begin(), jobs.end(), job);
                if (it != jobs.end()) jobs.erase(it);
                return false;
            }
            changed.wait(lock, [&] { return job->done; });
        }
        return job->succeeded;
    }

    void Drain() {
        // Bound work per callback so clients cannot monopolise a game tick.
        for (int n = 0; n < 16; ++n) {
            std::shared_ptr<Job> job;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (jobs.empty()) return;
                job = jobs.front();
                jobs.pop_front();
                job->started = true;
            }
            SyncQueue* previous = executing;
            executing = this;
            bool succeeded = false;
            try { job->work(); succeeded = true; } catch (...) {}
            executing = previous;
            {
                std::lock_guard<std::mutex> lock(mutex);
                job->succeeded = succeeded;
                job->done = true;
            }
            changed.notify_all();
        }
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(mutex);
        stopped = true;
        for (auto& job : jobs) job->done = true;
        jobs.clear();
        changed.notify_all();
    }
};
