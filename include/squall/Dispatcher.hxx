#ifndef SQUALL__DISPATCHER_HXX
#define SQUALL__DISPATCHER_HXX

#include <memory>
#include <utility>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "NonCopyable.hxx"
#include "PlatformLoop.hxx"
#include "PlatformWatchers.hxx"

using std::placeholders::_1;
using std::placeholders::_2;


namespace squall {
namespace exc {

class CannotSetupWatching : public std::runtime_error {
  public:
    CannotSetupWatching(std::string message = "")
        : std::runtime_error(message.size() > 0 ? message : "Error while set up event watching") {}
};
}


/* Contexted event dispatcher. */
template <typename Ctx>
class Dispatcher : NonCopyable {

    using CtxTarget = std::function<void(Ctx ctx, int revents, void* payload)>;

  public:
    /** Returns true if this dispatcher is active. */
    bool active() const noexcept {
        return (sp_loop.get() != nullptr);
    };


    /**
     * Returns const reference to using shared pointer to event loop.
     */
    const std::shared_ptr<PlatformLoop>& sharedLoop() const noexcept {
        return sp_loop;
    }


    /** Constructor */
    Dispatcher(CtxTarget&& ctx_target, const std::shared_ptr<PlatformLoop>& sp_loop)
        : ctx_target(std::forward<CtxTarget>(ctx_target)), sp_loop(sp_loop) {}


    /** Destructor */
    virtual ~Dispatcher() {
        release();
    }

    /**
     * Stops and releases all associated watchers and buffers.
     */
    void release() {
        if (active()) {
            std::unordered_set<Ctx> ctx_to_cleanup;
            for (auto const& pair : io_watchers)
                ctx_to_cleanup.insert(pair.first);
            for (auto const& pair : timer_watchers)
                ctx_to_cleanup.insert(pair.first);
            for (auto const& pair : signal_watchers)
                ctx_to_cleanup.insert(pair.first);
            for (auto const& ctx : ctx_to_cleanup) {
                cancelIoWatching(ctx);
                cancelTimerWatching(ctx);
                cancelSignalWatching(ctx);
                ctx_target(ctx, Event::CLEANUP, nullptr);
            }
            sp_loop.reset();
        }
    }

    /**
     * Setup to call event handler for a given `ctx`
     * when the I/O device with a given `fd` would be to read and/or write `mode`.
     */
    void setupIoWatching(Ctx ctx, int fd, int mode) {
        if (active()) {
            auto found = io_watchers.find(ctx);
            if (found != io_watchers.end()) {
                auto p_watcher = found->second.get();
                if (p_watcher->setup(fd, mode))
                    return;
            } else {
                auto up_watcher = std::unique_ptr<IoWatcher>(
                    new IoWatcher(std::bind(ctx_target, ctx, _1, _2), sharedLoop()));
                if (up_watcher->setup(fd, mode)) {
                    auto result = io_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
                    if (result.second)
                        return;
                    up_watcher->cancel();
                }
            }
        }
        throw exc::CannotSetupWatching();
    }

    /**
     * Updates I/O mode for event watchig established
     * with method `setup_io` for a given `ctx`.
     */
    bool updateIoWatching(Ctx ctx, int mode) {
        if (active()) {
            auto found = io_watchers.find(ctx);
            if (found != io_watchers.end()) {
                auto p_watcher = found->second.get();
                if (p_watcher->setup(p_watcher->fd(), mode))
                    return true;
            }
        }
        return false;
    }

    /**
     * Cancels an event watchig established
     * with method `setup_io` for a given `ctx`.
     */
    bool cancelIoWatching(Ctx ctx) {
        if (active()) {
            auto found = io_watchers.find(ctx);
            if (found != io_watchers.end()) {
                found->second.get()->cancel();
                io_watchers.erase(found);
                return true;
            }
        }
        return false;
    }

    /**
     * Setup to call event handler for a given `ctx`
     * every `seconds`.
     */
    void setupTimerWatching(Ctx ctx, double seconds) {
        if (active()) {
            seconds = (seconds > 0) ? seconds : 0;
            auto found = timer_watchers.find(ctx);
            if (found != timer_watchers.end()) {
                auto p_watcher = found->second.get();
                if (p_watcher->setup(seconds, seconds))
                    return;
            } else {
                auto up_watcher = std::unique_ptr<TimerWatcher>(
                    new TimerWatcher(std::bind(ctx_target, ctx, _1, _2), sharedLoop()));
                if (up_watcher->setup(seconds, seconds)) {
                    auto result = timer_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
                    if (result.second)
                        return;
                    up_watcher->cancel();
                }
            }
        }
        throw exc::CannotSetupWatching();
    }

    /**
     * Cancels an event watchig established
     * with method `setup_timer` for a given `ctx`.
     */
    bool cancelTimerWatching(Ctx ctx) {
        if (active()) {
            auto found = timer_watchers.find(ctx);
            if (found != timer_watchers.end()) {
                found->second.get()->cancel();
                timer_watchers.erase(found);
                return true;
            }
        }
        return false;
    }

    /**
     * Setup to call event handler for a given `ctx`
     * when the system signal with a given `signum` recieved.
     */
    void setupSignalWatching(Ctx ctx, int signum) {
        if (active()) {
            auto found = signal_watchers.find(ctx);
            if (found != signal_watchers.end()) {
                auto p_watcher = found->second.get();
                if (p_watcher->setup(signum))
                    return;
            } else {
                auto up_watcher = std::unique_ptr<SignalWatcher>(
                    new SignalWatcher(std::bind(ctx_target, ctx, _1, _2), sharedLoop()));
                if (up_watcher->setup(signum)) {
                    auto result = signal_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
                    if (result.second)
                        return;
                    up_watcher->cancel();
                }
            }
        }
        throw exc::CannotSetupWatching();
    }

    /**
     * Cancels an event watchig established
     * with method `setup_signal` for a given `ctx`.
     */
    bool cancelSignalWatching(Ctx ctx) {
        if (active()) {
            auto found = signal_watchers.find(ctx);
            if (found != signal_watchers.end()) {
                found->second.get()->cancel();
                signal_watchers.erase(found);
                return true;
            }
        }
        return false;
    }

  private:
    CtxTarget ctx_target;
    std::shared_ptr<PlatformLoop> sp_loop;
    std::unordered_map<Ctx, std::unique_ptr<IoWatcher>> io_watchers;
    std::unordered_map<Ctx, std::unique_ptr<TimerWatcher>> timer_watchers;
    std::unordered_map<Ctx, std::unique_ptr<SignalWatcher>> signal_watchers;
};
}
#endif // SQUALL__DISPATCHER_HXX