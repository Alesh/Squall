#ifndef SQUALL__DISPATCHER_HXX
#define SQUALL__DISPATCHER_HXX

#include <memory>
#include <utility>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "NonCopyable.hxx"
#include "EventLoop.hxx"

namespace squall {


/* Contexted event dispatcher. */
template <typename Ctx>
class Dispatcher : NonCopyable {

    using CtxTarget = std::function<void(Ctx ctx, int revents)>;

    template <typename EV>
    struct Watcher {
        EV ev;
        Dispatcher* disp;
    };

  public:
    /** Returns true if this dispatcher is active. */
    bool active() noexcept {
        return (sp_loop.get() != nullptr);
    };

    /**
     * Returns const reference to using shared pointer to event loop.
     */
    const std::shared_ptr<EventLoop>& shared_loop() noexcept {
        return sp_loop;
    }


    /** Constructor */
    Dispatcher(CtxTarget&& ctx_target, const std::shared_ptr<EventLoop>& sp_loop)
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
            io_watchers.clear();
            timer_watchers.clear();
            signal_watchers.clear();
            for (auto const& ctx : ctxs)
                ctx_target(ctx, Event::Cleanup);
            sp_loop.reset();
        }
    }

    /**
     * Setup to call event handler for a given `ctx`
     * when the I/O device with a given `fd` would be to read and/or write `mode`.
     */
    Ctx setup_io(Ctx ctx, int fd, int mode) {
        auto up_watcher = std::unique_ptr<Watcher<ev_io>>(new Watcher<ev_io>({{}, this}));
        auto p_watcher = &(up_watcher.get()->ev);
        ev_io_init(p_watcher, Dispatcher::template callback<ev_io>, fd, mode);
        if (auto p_loop = sp_loop.get())
            ev_io_start(p_loop->raw(), p_watcher);
        if (ev_is_active(p_watcher)) {
            auto result = io_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
            if (result.second) {
                result.first->second.get()->ev.data = (void*)(&(result.first->first));
                return ctx;
            }
        }
        return nullable_ctx();
    }

    /**
     * Updates I/O mode for event watchig established
     * with method `setup_io` for a given `ctx`.
     */
    bool update_io(Ctx ctx, int mode) {
        auto found = io_watchers.find(ctx);
        if (found != io_watchers.end()) {
            auto p_watcher = &(found->second.get()->ev);
            if (auto p_loop = sp_loop.get()) {
                if (ev_is_active(p_watcher))
                    ev_io_stop(p_loop->raw(), p_watcher);
            }
            ev_io_set(p_watcher, p_watcher->fd, mode);
            if (auto p_loop = sp_loop.get())
                ev_io_start(p_loop->raw(), p_watcher);
            if (ev_is_active(p_watcher))
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
        if (found != io_watchers.end()) {
            auto p_watcher = &(found->second.get()->ev);
            if (auto p_loop = sp_loop.get()) {
                if (ev_is_active(p_watcher))
                    ev_io_stop(p_loop->raw(), p_watcher);
            }
            io_watchers.erase(found);
            return true;
        }
        return false;
    }

    /**
     * Setup to call event handler for a given `ctx`
     * every `seconds`.
     */
    Ctx setup_timer(Ctx ctx, double seconds) {
        seconds = (seconds > 0) ? seconds : 0;
        auto up_watcher = std::unique_ptr<Watcher<ev_timer>>(new Watcher<ev_timer>({{}, this}));
        auto p_watcher = &(up_watcher.get()->ev);
        if (auto p_loop = sp_loop.get()) {
            ev_timer_init(p_watcher, Dispatcher::template callback<ev_timer>,
                          seconds + (ev_time() - ev_now(p_loop->raw())), seconds);
            ev_timer_start(p_loop->raw(), p_watcher);
        }
        if (ev_is_active(p_watcher)) {
            auto result = timer_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
            if (result.second) {
                result.first->second.get()->ev.data = (void*)(&(result.first->first));
                return ctx;
            }
        }
        return nullable_ctx();
    }

    /**
     * Updates timeout for event watchig established
     * with method `setup_timer` for a given `ctx`.
     */
    bool update_timer(Ctx ctx, double seconds) {
        seconds = (seconds > 0) ? seconds : 0;
        auto found = timer_watchers.find(ctx);
        if (found != timer_watchers.end()) {
            auto p_watcher = &(found->second.get()->ev);
            if (auto p_loop = sp_loop.get()) {
                if (ev_is_active(p_watcher))
                    ev_timer_stop(p_loop->raw(), p_watcher);
            }
            if (auto p_loop = sp_loop.get()) {
                ev_timer_set(p_watcher, seconds + (ev_time() - ev_now(p_loop->raw())), seconds);
                ev_timer_start(p_loop->raw(), p_watcher);
            }
            if (ev_is_active(p_watcher))
                return true;
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
            auto p_watcher = &(found->second.get()->ev);
            if (auto p_loop = sp_loop.get()) {
                if (ev_is_active(p_watcher))
                    ev_timer_stop(p_loop->raw(), p_watcher);
            }
            timer_watchers.erase(found);
            return true;
        }
        return false;
    }

    /**
     * Setup to call event handler for a given `ctx`
     * when the system signal with a given `signum` recieved.
     */
    Ctx setup_signal(Ctx ctx, int signum) {
        auto up_watcher = std::unique_ptr<Watcher<ev_signal>>(new Watcher<ev_signal>({{}, this}));
        auto p_watcher = &(up_watcher.get()->ev);
        ev_signal_init(p_watcher, Dispatcher::template callback<ev_signal>, signum);
        if (auto p_loop = sp_loop.get())
            ev_signal_start(p_loop->raw(), p_watcher);
        if (ev_is_active(p_watcher)) {
            auto result = signal_watchers.insert(std::make_pair(ctx, std::move(up_watcher)));
            if (result.second) {
                result.first->second.get()->ev.data = (void*)(&(result.first->first));
                return ctx;
            }
        }
        return nullable_ctx();
    }

    /**
     * Cancels an event watchig established
     * with method `setup_signal` for a given `ctx`.
     */
    bool cancel_signal(Ctx ctx) {
        auto found = signal_watchers.find(ctx);
        if (found != signal_watchers.end()) {
            auto p_watcher = &(found->second.get()->ev);
            if (auto p_loop = sp_loop.get()) {
                if (ev_is_active(p_watcher))
                    ev_signal_stop(p_loop->raw(), p_watcher);
            }
            signal_watchers.erase(found);
            return true;
        }
        return false;
    }

  protected:
    Ctx nullable_ctx() noexcept;

  private:
    CtxTarget ctx_target;
    std::shared_ptr<EventLoop> sp_loop;
    std::unordered_map<Ctx, std::unique_ptr<Watcher<ev_io>>> io_watchers;
    std::unordered_map<Ctx, std::unique_ptr<Watcher<ev_timer>>> timer_watchers;
    std::unordered_map<Ctx, std::unique_ptr<Watcher<ev_signal>>> signal_watchers;

    template <typename EV>
    static void callback(struct ev_loop* p_loop, EV* p_ev_watcher, int revents) {
        auto p_ctx = static_cast<Ctx*>(p_ev_watcher->data);
        auto p_watcher = reinterpret_cast<Watcher<EV>*>(p_ev_watcher);
        p_watcher->disp->ctx_target(*p_ctx, revents);
    }
};

}
#endif // SQUALL__DISPATCHER_HXX