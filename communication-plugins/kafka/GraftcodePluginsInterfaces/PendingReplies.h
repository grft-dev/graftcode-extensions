/**
 * @file PendingReplies.h
 * @brief Correlates concurrent requests with replies delivered by a single consumer.
 *
 * A caller registers its correlation id before sending, then waits on the returned ticket.
 * The consumer completes or fails entries by id. The ticket removes its entry when it goes
 * out of scope, so timed-out and failed requests leave nothing behind.
 */

#pragma once

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Graftcode::Plugins::Concurrency
{
    class PendingReplies
    {
        struct Slot
        {
            std::condition_variable ready;
            bool done{ false };
            bool failed{ false };
            std::vector<unsigned char> payload;
            std::string error;
        };

    public:
        using Payload = std::vector<unsigned char>;

        class Ticket
        {
        public:
            Ticket(PendingReplies& owner, std::string id, std::shared_ptr<Slot> slot)
                : owner_(&owner), id_(std::move(id)), slot_(std::move(slot))
            {
            }

            ~Ticket()
            {
                if (owner_ != nullptr) {
                    owner_->Remove(id_);
                }
            }

            Ticket(Ticket&& other) noexcept
                : owner_(std::exchange(other.owner_, nullptr)),
                  id_(std::move(other.id_)),
                  slot_(std::move(other.slot_))
            {
            }

            Ticket(const Ticket&) = delete;
            Ticket& operator=(const Ticket&) = delete;
            Ticket& operator=(Ticket&&) = delete;

            const std::string& Id() const { return id_; }

            // Returns false on timeout; throws when the request was failed.
            bool Wait(std::chrono::milliseconds timeout, Payload& response)
            {
                std::unique_lock<std::mutex> lock(owner_->mutex_);
                if (!slot_->ready.wait_for(lock, timeout, [this] { return slot_->done; })) {
                    return false;
                }
                if (slot_->failed) {
                    throw std::runtime_error(slot_->error);
                }
                response = std::move(slot_->payload);
                return true;
            }

        private:
            PendingReplies* owner_;
            std::string id_;
            std::shared_ptr<Slot> slot_;
        };

        PendingReplies() = default;
        PendingReplies(const PendingReplies&) = delete;
        PendingReplies& operator=(const PendingReplies&) = delete;

        Ticket Register(const std::string& id)
        {
            auto slot = std::make_shared<Slot>();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (closed_) {
                    throw std::runtime_error(closedError_);
                }
                if (!slots_.emplace(id, slot).second) {
                    throw std::runtime_error("duplicate correlation id " + id);
                }
            }
            pendingChanged_.notify_all();
            return Ticket(*this, id, std::move(slot));
        }

        // Returns false when no request is waiting for this id.
        bool Complete(const std::string& id, Payload payload)
        {
            return Finish(id, false, std::move(payload), std::string());
        }

        bool Fail(const std::string& id, const std::string& error)
        {
            return Finish(id, true, Payload(), error);
        }

        void FailAll(const std::string& error)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& entry : slots_) {
                MarkDone(*entry.second, true, Payload(), error);
            }
        }

        // Fails every waiting request and rejects new registrations.
        void Close(const std::string& error)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                closed_ = true;
                closedError_ = error;
                for (auto& entry : slots_) {
                    MarkDone(*entry.second, true, Payload(), error);
                }
            }
            pendingChanged_.notify_all();
        }

        bool Empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return slots_.empty();
        }

        // Blocks until a request is registered, the set is closed, or the timeout elapses.
        // Returns true when at least one request is waiting.
        bool WaitForPending(std::chrono::milliseconds timeout)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            pendingChanged_.wait_for(lock, timeout, [this] { return closed_ || !slots_.empty(); });
            return !closed_ && !slots_.empty();
        }

    private:
        static void MarkDone(Slot& slot, bool failed, Payload payload, const std::string& error)
        {
            if (slot.done) {
                return;
            }
            slot.done = true;
            slot.failed = failed;
            slot.payload = std::move(payload);
            slot.error = error;
            slot.ready.notify_all();
        }

        bool Finish(const std::string& id, bool failed, Payload payload, const std::string& error)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = slots_.find(id);
            if (it == slots_.end()) {
                return false;
            }
            MarkDone(*it->second, failed, std::move(payload), error);
            return true;
        }

        void Remove(const std::string& id)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            slots_.erase(id);
        }

        mutable std::mutex mutex_;
        std::condition_variable pendingChanged_;
        std::map<std::string, std::shared_ptr<Slot>> slots_;
        bool closed_{ false };
        std::string closedError_;
    };
}
