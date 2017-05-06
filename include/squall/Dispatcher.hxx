#ifndef SQUALL__DISPATCHER_HXX
#define SQUALL__DISPATCHER_HXX

#include <memory>
#include <utility>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "NonCopyable.hxx"
#include "PlatformLoop.hxx"
#include "PlatformWatcher.hxx"
#include "EventBuffer.hxx"

using std::placeholders::_1;


namespace squall {


/* Contexted event dispatcher. */
template <typename Ctx>
class Dispatcher : NonCopyable {

    using CtxTarget = std::function<void(Ctx ctx, int revents)>;

  public:
    /** Returns true if this dispatcher is active. */
    bool active() noexcept {
        return (sp_loop.get() != nullptr);
    };

    /**
     * Returns const reference to using shared pointer to event loop.
     */
    const std::shared_ptr<PlatformLoop>& shared_loop() noexcept {
        return sp_loop;
    }


    /** Constructor */
    Dispatcher(CtxTarget&& ctx_target, const std::shared_ptr<PlatformLoop>& sp_loop)
        : ctx_target(std::forward<CtxTarget>(ctx_target)), sp_loop(sp_loop) {}


    /** Destructor */
    virtual ~Dispatcher() {
        cleanup();
    }

    /**
     * Stops and cleanups all associated watchers.
     */
    void cleanup() {
        if (sp_loop.get() != nullptr) {
            std::unordered_set<Ctx> ctxs;
            for (auto const& pair : io_watchers)
                ctxs.insert(pair.first);
            for (auto const& pair : timer_watchers)
                ctxs.insert(pair.first);
            for (auto const& pair : signal_watchers)
                ctxs.insert(pair.first);
            for (auto const& ctx : ctxs) {
                if (!cancel_io(ctx))
                    release_buffer(ctx);
                cancel_timer(ctx);
                cancel_signal(ctx);
                ctx_target(ctx, Event::Cleanup);
            }
            sp_loop.reset();
        }
    }

    /**
     * Setup to call event handler for a given `ctx`
     * when the I/O device with a given `fd` would be to read and/or write `mode`.
     */
    bool setup_io(Ctx ctx, int fd, int mode) {
        auto found = io_watchers.find(ctx);
        if (found != io_watchers.end()) {
            if (found->second.get()->is_buffer())
                return false; // cannot owerride buffer
            auto p_watcher = found->second.get();
            if (p_watcher->setup(fd, mode))
                return true;
        } else {
            auto up_watcher =
                std::unique_ptr<IoWatcher>(new IoWatcher(std::bind(ctx_target, ctx, _1), shared_loop()));
            if (up_watcher->setup(fd, mode)) {
                auto result = io_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
                if (result.second)
                    return true;
                up_watcher->cancel();
            }
        }
        return false;
    }

    /**
     * Updates I/O mode for event watchig established
     * with method `setup_io` for a given `ctx`.
     */
    bool update_io(Ctx ctx, int mode) {
        auto found = io_watchers.find(ctx);
        if (found != io_watchers.end() && (!found->second.get()->is_buffer())) {
            auto p_watcher = found->second.get();
            if (p_watcher->setup(p_watcher->fd(), mode))
                return true;
        }
        return false;
    }

    /**
     * Cancels an event watchig established
     * with method `setup_io` for a given `ctx`.
     */
    bool cancel_io(Ctx ctx) {
        auto found = io_watchers.find(ctx);
        if (found != io_watchers.end() && (!found->second.get()->is_buffer())) {
            found->second.get()->cancel();
            io_watchers.erase(found);
            return true;
        }
        return false;
    }

    /**
     * Setup to call event handler for a given `ctx`
     * every `seconds`.
     */
    bool setup_timer(Ctx ctx, double seconds) {
        seconds = (seconds > 0) ? seconds : 0;
        auto found = timer_watchers.find(ctx);
        if (found != timer_watchers.end()) {
            auto p_watcher = found->second.get();
            if (p_watcher->setup(seconds, seconds))
                return true;
        } else {
            auto up_watcher = std::unique_ptr<TimerWatcher>(
                new TimerWatcher(std::bind(ctx_target, ctx, _1), shared_loop()));
            if (up_watcher->setup(seconds, seconds)) {
                auto result = timer_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
                if (result.second)
                    return true;
                up_watcher->cancel();
            }
        }
        return false;
    }

    /**
     * Cancels an event watchig established
     * with method `setup_timer` for a given `ctx`.
     */
    bool cancel_timer(Ctx ctx) {
        auto found = timer_watchers.find(ctx);
        if (found != timer_watchers.end()) {
            found->second.get()->cancel();
            timer_watchers.erase(found);
            return true;
        }
        return false;
    }

    /**
     * Setup to call event handler for a given `ctx`
     * when the system signal with a given `signum` recieved.
     */
    bool setup_signal(Ctx ctx, int signum) {
        auto found = signal_watchers.find(ctx);
        if (found != signal_watchers.end()) {
            auto p_watcher = found->second.get();
            if (p_watcher->setup(signum))
                return true;
        } else {
            auto up_watcher = std::unique_ptr<SignalWatcher>(
                new SignalWatcher(std::bind(ctx_target, ctx, _1), shared_loop()));
            if (up_watcher->setup(signum)) {
                auto result = signal_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
                if (result.second)
                    return true;
                up_watcher->cancel();
            }
        }
        return false;
    }

    /**
     * Cancels an event watchig established
     * with method `setup_signal` for a given `ctx`.
     */
    bool cancel_signal(Ctx ctx) {
        auto found = signal_watchers.find(ctx);
        if (found != signal_watchers.end()) {
            found->second.get()->cancel();
            signal_watchers.erase(found);
            return true;
        }
        return false;
    }


    /** Creates event buffer and returns reference to it. */
    template <typename BufferType = EventBuffer>
    EventBuffer* create_buffer(Ctx ctx, int fd, size_t block_size = 0, size_t buffer_size = 0) {
        auto found = io_watchers.find(ctx);
        if (found != io_watchers.end()) {
            if (found->second.get()->is_buffer())
                release_buffer(static_cast<EventBuffer*>(found->second.get()));
            else
                cancel_io(ctx);
        }
        std::unique_ptr<IoWatcher> up_watcher = std::unique_ptr<IoWatcher>(
            new BufferType(std::bind(ctx_target, ctx, _1), shared_loop(), fd, block_size, buffer_size));
        if (up_watcher->active()) {
            auto p_watcher = up_watcher.get();
            auto p_buffer = static_cast<EventBuffer*>(p_watcher);;
            auto result = io_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
            if (result.second) {
                if (back_buffers.insert(std::make_pair(p_buffer, &ctx)).second)
                    return p_buffer;
            }
            p_watcher->cancel();
        }
        return nullptr;
    }

    /** Released an event buffer. */
    bool release_buffer(EventBuffer* p_buffer) {
        auto found = back_buffers.find(p_buffer);
        if (found != back_buffers.end())
            return release_buffer(*(found->second));
        return false;
    }

    /** Released an event buffer binded with `ctx`. */
    bool release_buffer(Ctx ctx) {
        auto found = io_watchers.find(ctx);
        if (found != io_watchers.end() && (found->second.get()->is_buffer())) {
            back_buffers.erase(static_cast<EventBuffer*>(found->second.get()));
            found->second.get()->cancel();
            io_watchers.erase(found);
            return true;
        }
        return false;
    }


  private:
    CtxTarget ctx_target;
    std::shared_ptr<PlatformLoop> sp_loop;
    std::unordered_map<Ctx, std::unique_ptr<IoWatcher>> io_watchers;
    std::unordered_map<Ctx, std::unique_ptr<TimerWatcher>> timer_watchers;
    std::unordered_map<Ctx, std::unique_ptr<SignalWatcher>> signal_watchers;
    std::unordered_map<EventBuffer*, Ctx*> back_buffers;
};
}
#endif // SQUALL__DISPATCHER_HXX