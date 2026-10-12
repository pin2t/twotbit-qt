#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "util/bytes.h"

namespace tb {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration = Clock::duration;
using namespace std::chrono_literals;

/// TimeoutError is a wait that ran out of time.
class TimeoutError : public Error {
public:
    explicit TimeoutError(const std::string& what = "timed out") : Error(what) {}
};

/// CancellationError is work given up because its task was cancelled.
class CancellationError : public Error {
public:
    CancellationError() : Error("cancelled") {}
};

/// The whole milliseconds of the duration.
int64_t toMilliseconds(Duration d);

/// The duration as Go prints one: "5s", "1m30s", "250ms".
std::string formatDuration(Duration d);

/// The description of a caught exception.
std::string describe(const std::exception_ptr& error);

// Every thread the engine starts runs as a task: it has a cancellation token,
// kept in a thread-local as Swift keeps a task's, so the blocking waits below
// return early once their task is cancelled. A task's token follows the one
// of the thread that started it.
namespace task {

/// The cancellation token of the calling thread; one never cancelled when
/// the thread was not started as a task.
std::stop_token token();

bool isCancelled();

/// Throws CancellationError once the calling task is cancelled.
void checkCancelled();

/// Sleeps for the duration and reports whether it slept through, false
/// when the task was cancelled.
bool sleep(Duration d);

/// TokenScope makes the token the calling thread's while it lives.
class TokenScope {
public:
    explicit TokenScope(std::stop_token token);
    ~TokenScope();
    TokenScope(const TokenScope&) = delete;
    TokenScope& operator=(const TokenScope&) = delete;

private:
    std::stop_token token_;
    const std::stop_token* previous_;
};

} // namespace task

/// Task is work running on a thread of its own, cancelled with the task that
/// started it or by cancel(). Destroying it cancels it and waits for it.
class Task {
public:
    Task() = default;
    explicit Task(std::function<void()> work, std::stop_token parent = task::token());
    Task(Task&& other) noexcept = default;
    Task& operator=(Task&& other) noexcept;
    ~Task();

    void cancel();
    void join();
    bool valid() const { return state_ != nullptr; }
    bool done() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

/// TaskGroup runs work on threads of their own, as Swift's task groups do:
/// cancelled together and with the task that made the group, and waited for
/// when the group goes away.
class TaskGroup {
public:
    explicit TaskGroup(std::stop_token parent = task::token());
    ~TaskGroup();
    TaskGroup(const TaskGroup&) = delete;
    TaskGroup& operator=(const TaskGroup&) = delete;

    void add(std::function<void()> work);

    /// Waits until another task finished and reports whether one did:
    /// false once every task added finished and was reported.
    bool next();

    void waitAll();
    void cancelAll();
    std::stop_token token() const { return source_.get_token(); }

private:
    std::stop_source source_;
    std::unique_ptr<std::stop_callback<std::function<void()>>> link_;
    std::mutex mutex_;
    std::condition_variable cv_;
    size_t added_ = 0, finished_ = 0, reported_ = 0;
    std::vector<std::thread> threads_;
};

/// Mailbox hands the messages a peer delivers to the request waiting for
/// them, buffering those that arrive first, like a Go channel.
template <class T>
class Mailbox {
public:
    /// capacity is the most items kept for a later request; unsolicited ones
    /// beyond it are dropped.
    explicit Mailbox(size_t capacity = 1024) : capacity_(capacity) {}

    /// Keeps the item for the next receive.
    void deliver(T item) {
        {
            std::lock_guard lock(mutex_);
            if (closed_ || items_.size() >= capacity_) return;
            items_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    /// Discards the items left by an earlier request.
    void drain() {
        std::lock_guard lock(mutex_);
        items_.clear();
    }

    /// Fails every waiter, and every later receive, with the error.
    void close(std::exception_ptr error) {
        {
            std::lock_guard lock(mutex_);
            closed_ = std::move(error);
        }
        cv_.notify_all();
    }

    /// The next item within the timeout. It throws the close error once the
    /// mailbox is closed, CancellationError when the task is cancelled and
    /// TimeoutError naming what was waited for after the timeout.
#if defined(__GNUC__) && !defined(__clang__)
    // GCC at -O3 takes moving an empty std::optional out of the queue, as a
    // block reply without its block, for a read of the missing value.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    T receive(Duration timeout, const std::string& what) {
        std::unique_lock lock(mutex_);
        std::stop_token token = task::token();
        cv_.wait_until(lock, token, Clock::now() + timeout, [&] { return !items_.empty() || closed_ != nullptr; });
        if (!items_.empty()) {
            T item = std::move(items_.front());
            items_.pop_front();
            return item;
        }
        if (closed_) std::rethrow_exception(closed_);
        if (token.stop_requested()) throw CancellationError();
        throw TimeoutError("no " + what + " response within " + formatDuration(timeout));
    }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

private:
    const size_t capacity_;
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<T> items_;
    std::exception_ptr closed_;
};

/// Signal is a one-shot event: it fires once, as a closed Go channel, and
/// wakes every waiter.
class Signal {
public:
    bool isFired() const;

    /// Fires the signal and reports whether this call did.
    bool fire();

    /// Waits until the signal fires or the task is cancelled.
    void wait();

    /// Waits until the signal fires, the deadline passes or the task is
    /// cancelled, and reports whether it fired.
    bool waitUntil(TimePoint deadline);

private:
    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    bool fired_ = false;
};

/// Pulse wakes one waiting loop, as a Go channel with a buffer of one: a
/// signal sent while nobody waits is kept, and further ones merge into it.
class Pulse {
public:
    void signal();

    /// Waits for a signal, at most the timeout when one is given. It reports
    /// whether a signal came, false on timeout or cancellation.
    bool wait(std::optional<Duration> timeout = std::nullopt);

private:
    std::mutex mutex_;
    std::condition_variable_any cv_;
    bool pending_ = false;
};

} // namespace tb
