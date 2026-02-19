#pragma once

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#else
#include <condition_variable>
#include <mutex>
#endif

namespace squeeze {

class Semaphore {
public:
#ifdef __APPLE__
    Semaphore()  { sem_ = dispatch_semaphore_create(0); }
    ~Semaphore() { dispatch_release(sem_); }

    // RT-safe: dispatch_semaphore_signal is async-signal-safe on macOS
    void post() { dispatch_semaphore_signal(sem_); }

    // Blocking wait
    void wait() { dispatch_semaphore_wait(sem_, DISPATCH_TIME_FOREVER); }

private:
    dispatch_semaphore_t sem_;
#else
    Semaphore() = default;
    ~Semaphore() = default;

    void post()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++count_;
        cv_.notify_one();
    }

    void wait()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return count_ > 0; });
        --count_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    int count_ = 0;
#endif

    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
};

} // namespace squeeze
