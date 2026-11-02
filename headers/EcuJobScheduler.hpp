#pragma once
#include "json.hpp"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

// Deterministic, single-CPU, non-preemptive FIFO of frozen ECU-cycle jobs.
// Host elapsed time is deliberately absent. All times are simulation microseconds.
class EcuJobScheduler {
    using Json = nlohmann::json;
public:
    static uint64_t integer(const Json &v, const std::string &name) {
        if (!v.is_number_integer() || (!v.is_number_unsigned() && v.get<int64_t>() < 0))
            throw std::invalid_argument(name + " must be a nonnegative integer");
        return v.get<uint64_t>();
    }
    static uint64_t add(uint64_t a, uint64_t b) {
        if (b > std::numeric_limits<uint64_t>::max() - a) throw std::invalid_argument("Simulation time overflow");
        return a + b;
    }
    static uint64_t tickTime(uint64_t tick, unsigned stepMs) {
        if (!stepMs || tick > std::numeric_limits<uint64_t>::max() / (uint64_t(stepMs) * 1000))
            throw std::invalid_argument("Simulation tick overflow");
        return tick * uint64_t(stepMs) * 1000;
    }
    struct Config {
        uint64_t max_pending = 8, queue_bytes = 0, overhead_bytes = 256;
        uint64_t deadline_us = 100000, watchdog_us = 1000000;
    };
    static Config config(const Json &settings, uint64_t availableBytes) {
        if (!settings.is_object()) throw std::invalid_argument("Scheduler configuration must be an object");
        for (const auto &entry : settings.items()) {
            const auto &key = entry.key();
            if (key != "max_pending_jobs" && key != "queue_bytes" && key != "job_overhead_bytes" && key != "deadline_us" && key != "watchdog_us")
                throw std::invalid_argument("Unknown scheduler setting: " + key);
        }
        Config c;
        c.max_pending = integer(settings.value("max_pending_jobs", Json(8)), "max_pending_jobs");
        c.queue_bytes = integer(settings.value("queue_bytes", Json(availableBytes)), "queue_bytes");
        c.overhead_bytes = integer(settings.value("job_overhead_bytes", Json(256)), "job_overhead_bytes");
        c.deadline_us = integer(settings.value("deadline_us", Json(100000)), "deadline_us");
        c.watchdog_us = integer(settings.value("watchdog_us", Json(1000000)), "watchdog_us");
        if (!c.max_pending || c.max_pending > 4096 || !c.queue_bytes || c.queue_bytes > availableBytes ||
            c.overhead_bytes > c.queue_bytes || !c.deadline_us || c.deadline_us > 60000000 ||
            !c.watchdog_us || c.watchdog_us > 60000000)
            throw std::invalid_argument("Scheduler queue/RAM/deadline/watchdog limits out of range");
        return c;
    }
    void configure(Config config) { config_ = config; }
    void reset(uint64_t epoch, uint64_t now) {
        // Retain bounded cancellation evidence across reset; no old payload survives.
        if (active_) terminal(*active_, "cancelled", "epoch_reset", now_);
        for (auto &job : queue_) terminal(job, "cancelled", "epoch_reset", now_);
        queue_.clear(); active_.reset(); bytes_ = 0; epoch_ = epoch; now_ = now; sequence_ = 0; tripped_ = false; busy_ = 0;
    }
    void enqueue(const Json &frozen, uint64_t duration, const std::string &inputHash) {
        // Any admitted job can wait until its deadline before starting. Check the
        // whole representable horizon before changing identities or reservations.
        add(add(now_, config_.deadline_us), std::max(duration, config_.watchdog_us));
        Job job;
        job.id = add(sequence_, 1);
        job.epoch = epoch_; job.release = now_; job.duration = duration;
        job.deadline = add(now_, config_.deadline_us);
        job.bytes = add(config_.overhead_bytes, frozen.dump().size()); job.input_hash = inputHash;
        sequence_ = job.id;
        std::string reason;
        if (tripped_) reason = "watchdog_latched";
        else if (queue_.size() + bool(active_) >= config_.max_pending) reason = "queue_full";
        else if (job.bytes > config_.queue_bytes - bytes_) reason = "ram_exhausted";
        if (!reason.empty()) { terminal(job, "resource_rejected", reason, now_); return; }
        // Copy only after reserving the complete payload plus modeled bookkeeping.
        job.frozen = frozen; bytes_ += job.bytes;
        event(job, "queued", "none", now_); queue_.push_back(std::move(job));
    }
    Json advance(uint64_t end, const std::function<Json(const Json&, uint64_t, uint64_t)> &compute) {
        if (end < now_) throw std::invalid_argument("Scheduler cannot move backwards");
        busy_ = 0;
        Json completed = Json::array();
        for (;;) {
            if (!active_ && !queue_.empty()) {
                active_ = std::move(queue_.front()); queue_.pop_front();
                active_->start = now_; active_->started = true; active_->finish = add(now_, active_->duration);
                event(*active_, "running", "none", now_);
            }
            uint64_t next = end;
            if (active_) next = std::min({next, active_->finish, active_->deadline, add(active_->start, config_.watchdog_us)});
            for (const auto &job : queue_) next = std::min(next, job.deadline);
            if (active_) busy_ += next - now_;
            now_ = next;
            if (active_) {
                auto &job = *active_;
                if (job.finish <= now_ && job.finish <= job.deadline && job.duration <= config_.watchdog_us) {
                    try {
                        auto result = compute(job.frozen, job.start, job.finish);
                        result["job_id"] = job.id; result["input_hash"] = job.input_hash;
                        result["timing"].erase("task_budget_us");
                        result["timing"]["deadline_at_us"] = job.deadline;
                        completed.push_back(std::move(result));
                        terminal(job, "complete", "none", now_);
                    } catch (const std::exception &error) {
                        terminal(job, "failed", "evaluation_failed", now_);
                        events_.back()["error"] = std::string(error.what()).substr(0, 256);
                    }
                    bytes_ -= job.bytes; active_.reset();
                } else if (job.deadline <= now_) {
                    terminal(job, "deadline_failed", "deadline_miss", now_);
                    bytes_ -= job.bytes; active_.reset();
                } else if (add(job.start, config_.watchdog_us) <= now_) {
                    terminal(job, "failed", "watchdog", now_);
                    bytes_ -= job.bytes; active_.reset(); tripped_ = true;
                    for (auto &waiting : queue_) {
                        terminal(waiting, "cancelled", "watchdog", now_); bytes_ -= waiting.bytes;
                    }
                    queue_.clear();
                }
            }
            for (auto it = queue_.begin(); it != queue_.end();) {
                if (it->deadline <= now_) {
                    terminal(*it, "deadline_failed", "deadline_miss", now_); bytes_ -= it->bytes;
                    it = queue_.erase(it);
                } else ++it;
            }
            // Process immediate completions and start queued work even at a boundary.
            if (now_ == end && active_ && active_->finish > end) break;
            if (now_ == end && !active_ && queue_.empty()) break;
        }
        return completed;
    }
    Json status() const {
        Json pending = Json::array();
        if (active_) pending.push_back(describe(*active_, "running", "none", now_));
        for (const auto &job : queue_) pending.push_back(describe(job, "queued", "none", now_));
        return {{"epoch", epoch_}, {"sim_time_us", now_}, {"pending", pending}, {"reserved_bytes", bytes_},
            {"queue_bytes", config_.queue_bytes}, {"max_pending_jobs", config_.max_pending},
            {"busy_us", busy_}, {"watchdog_tripped", tripped_}, {"events", events_}, {"counters", counters_}};
    }
    uint64_t reservedBytes() const { return bytes_; }
    uint64_t busyUs() const { return busy_; }
    bool tripped() const { return tripped_; }
    bool hasPending() const { return active_.has_value() || !queue_.empty(); }
private:
    struct Job {
        uint64_t id = 0, epoch = 0, release = 0, start = 0, finish = 0, deadline = 0, duration = 0, bytes = 0;
        Json frozen;
        std::string input_hash;
        bool started = false;
    };
    Config config_;
    std::deque<Job> queue_;
    std::optional<Job> active_;
    uint64_t epoch_ = 0, now_ = 0, sequence_ = 0, bytes_ = 0, busy_ = 0;
    bool tripped_ = false;
    Json events_ = Json::array(), counters_ = Json::object();
    static Json describe(const Job &job, const std::string &state, const std::string &reason, uint64_t time) {
        Json result = {{"contract_version", 1}, {"epoch", job.epoch}, {"job_id", job.id}, {"state", state},
            {"reason", reason}, {"sim_time_us", time}, {"sampled_at_us", job.release},
            {"released_at_us", job.release}, {"deadline_at_us", job.deadline}, {"duration_us", job.duration},
            {"required_bytes", job.bytes}, {"reserved_bytes", job.frozen.is_null() ? 0 : job.bytes}, {"input_hash", job.input_hash}};
        if (job.started) result["started_at_us"] = job.start;
        if (state == "complete") result["computed_at_us"] = job.finish;
        return result;
    }
    void event(const Job &job, const std::string &state, const std::string &reason, uint64_t time) {
        events_.push_back(describe(job, state, reason, time));
        while (events_.size() > 128) events_.erase(events_.begin());
    }
    void terminal(const Job &job, const std::string &state, const std::string &reason, uint64_t time) {
        event(job, state, reason, time);
        std::string counter = state == "resource_rejected" ? reason : state;
        counters_[counter] = counters_.value(counter, 0ULL) + 1;
    }
};
