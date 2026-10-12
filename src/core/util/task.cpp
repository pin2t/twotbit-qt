#include "util/task.h"

#include <cstdio>

#include "util/log.h"

namespace tb {

int64_t toMilliseconds(Duration d) { return std::chrono::duration_cast<std::chrono::milliseconds>(d).count(); }

std::string formatDuration(Duration d) {
    int64_t ms = toMilliseconds(d);
    if (ms < 1000) return std::to_string(ms) + "ms";
    double seconds = double(ms) / 1000;
    if (seconds < 60) {
        if (ms % 1000 == 0) return std::to_string(ms / 1000) + "s";
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3gs", seconds);
        return buf;
    }
    int64_t whole = ms / 1000;
    if (whole % 60 == 0) return std::to_string(whole / 60) + "m0s";
    return std::to_string(whole / 60) + "m" + std::to_string(whole % 60) + "s";
}

std::string describe(const std::exception_ptr& error) {
    if (!error) return "no error";
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown error";
    }
}

namespace task {

namespace {
// A plain pointer to the token of the innermost scope: MinGW runs the
// destructors of thread-local objects from a TLS callback after a thread's
// state is gone, so the thread-local itself must not own anything.
thread_local const std::stop_token* current = nullptr;
}

std::stop_token token() { return current ? *current : std::stop_token(); }

bool isCancelled() { return current && current->stop_requested(); }

void checkCancelled() {
    if (isCancelled()) throw CancellationError();
}

bool sleep(Duration d) {
    std::mutex m;
    std::condition_variable_any cv;
    std::unique_lock lock(m);
    std::stop_token t = token();
    cv.wait_until(lock, t, Clock::now() + d, [] { return false; });
    return !t.stop_requested();
}

TokenScope::TokenScope(std::stop_token token) : token_(std::move(token)), previous_(current) { current = &token_; }

TokenScope::~TokenScope() { current = previous_; }

} // namespace task

namespace {

/// Runs one task's work with its token, logging what escapes it: a task's
/// failure must not end the process.
void runTask(const std::function<void()>& work, std::stop_token token) {
    task::TokenScope scope(std::move(token));
    try {
        work();
    } catch (const CancellationError&) {
    } catch (const std::exception& e) {
        Log::info(std::string("task failed: ") + e.what());
    } catch (...) {
        Log::info("task failed");
    }
}

} // namespace

struct Task::State {
    std::stop_source source;
    std::thread thread;
    std::atomic<bool> done{false};
};

Task::Task(std::function<void()> work, std::stop_token parent) : state_(std::make_shared<State>()) {
    std::shared_ptr<State> s = state_;
    s->thread = std::thread([s, work = std::move(work), parent = std::move(parent)] {
        std::stop_source source = s->source;
        {
            std::stop_callback link(parent, [source]() mutable { source.request_stop(); });
            runTask(work, source.get_token());
        }
        s->done = true;
    });
}

Task& Task::operator=(Task&& other) noexcept {
    if (this != &other) {
        cancel();
        join();
        state_ = std::move(other.state_);
    }
    return *this;
}

Task::~Task() {
    cancel();
    join();
}

void Task::cancel() {
    if (state_) state_->source.request_stop();
}

void Task::join() {
    if (!state_ || !state_->thread.joinable()) return;
    // A task let go of on its own thread lets the thread finish alone.
    if (state_->thread.get_id() == std::this_thread::get_id())
        state_->thread.detach();
    else
        state_->thread.join();
}

bool Task::done() const { return !state_ || state_->done; }

TaskGroup::TaskGroup(std::stop_token parent) {
    std::stop_source source = source_;
    link_ = std::make_unique<std::stop_callback<std::function<void()>>>(std::move(parent), [source]() mutable { source.request_stop(); });
}

TaskGroup::~TaskGroup() {
    cancelAll();
    waitAll();
    link_.reset();
}

void TaskGroup::add(std::function<void()> work) {
    std::lock_guard lock(mutex_);
    ++added_;
    threads_.emplace_back([this, work = std::move(work), token = source_.get_token()] {
        runTask(work, token);
        {
            std::lock_guard l(mutex_);
            ++finished_;
        }
        cv_.notify_all();
    });
}

bool TaskGroup::next() {
    std::unique_lock lock(mutex_);
    if (reported_ >= added_) return false;
    cv_.wait(lock, [&] { return finished_ > reported_; });
    ++reported_;
    return true;
}

void TaskGroup::waitAll() {
    std::vector<std::thread> threads;
    {
        std::lock_guard lock(mutex_);
        threads.swap(threads_);
    }
    for (auto& t : threads) t.join();
    std::lock_guard lock(mutex_);
    reported_ = added_;
}

void TaskGroup::cancelAll() { source_.request_stop(); }

bool Signal::isFired() const {
    std::lock_guard lock(mutex_);
    return fired_;
}

bool Signal::fire() {
    {
        std::lock_guard lock(mutex_);
        if (fired_) return false;
        fired_ = true;
    }
    cv_.notify_all();
    return true;
}

void Signal::wait() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, task::token(), [&] { return fired_; });
}

bool Signal::waitUntil(TimePoint deadline) {
    std::unique_lock lock(mutex_);
    return cv_.wait_until(lock, task::token(), deadline, [&] { return fired_; });
}

void Pulse::signal() {
    {
        std::lock_guard lock(mutex_);
        pending_ = true;
    }
    cv_.notify_all();
}

bool Pulse::wait(std::optional<Duration> timeout) {
    std::unique_lock lock(mutex_);
    std::stop_token token = task::token();
    if (timeout)
        cv_.wait_until(lock, token, Clock::now() + *timeout, [&] { return pending_; });
    else
        cv_.wait(lock, token, [&] { return pending_; });
    if (!pending_) return false;
    pending_ = false;
    return true;
}

} // namespace tb
