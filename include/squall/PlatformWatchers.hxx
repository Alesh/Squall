#ifndef SQUALL__PLATFORM_WATCHERS_HXX
#define SQUALL__PLATFORM_WATCHERS_HXX

#include "PlatformLoop.hxx"

namespace squall {

/* Common event watcher */
template <typename EV>
class Watcher {
  protected:
    EV ev;
    OnEvent on_event;
    struct ev_loop* p_loop;

    static void callback(struct ev_loop* p_loop, EV* p_ev_watcher, int revents) {
        auto p_watcher = reinterpret_cast<Watcher<EV>*>(p_ev_watcher);
        p_watcher->on_event(revents, (void*)p_watcher);
    }

  public:
    /* Return true if this is active. */
    bool active() noexcept {
        return (ev_is_active(&ev) != 0);
    }

    /* Constructor */
    Watcher(OnEvent&& on_event, const std::shared_ptr<PlatformLoop>& sp_loop)
        : ev({}), on_event(std::forward<OnEvent>(on_event)), p_loop(sp_loop->raw) {
        ev_init(&ev, Watcher::callback);
    }

    /* Destructor */
    ~Watcher() {}

    /* Sets up to starts an event watching. */
    template <typename... Args>
    bool setup(Args... args);

    /* Cancels an event watching. */
    bool cancel();
};


template <>
inline bool Watcher<ev_io>::cancel() {
    if (active()) {
        ev_io_stop(p_loop, &ev);
        return true;
    }
    return false;
}

template <>
template <>
inline bool Watcher<ev_io>::setup<int, int>(int fd, int mode) {
    if (active())
        cancel();
    mode = (mode & (EV_READ | EV_WRITE));
    ev_io_set(&ev, fd, mode);
    if ((fd >= 0) && (mode > 0)) {
        ev_io_start(p_loop, &ev);
        return active();
    }
    return false;
}


template <>
inline bool Watcher<ev_timer>::cancel() {
    if (active()) {
        ev_timer_stop(p_loop, &ev);
        return true;
    }
    return false;
}

template <>
template <>
inline bool Watcher<ev_timer>::setup<double, double>(double after, double repeat) {
    if (active())
        cancel();
    if (after >= 0) {
        ev_timer_set(&ev, after + (ev_time() - ev_now(p_loop)), (repeat > 0 ? repeat : 0));
        ev_timer_start(p_loop, &ev);
        return active();
    }
    return false;
}


template <>
inline bool Watcher<ev_signal>::cancel() {
    if (active()) {
        ev_signal_stop(p_loop, &ev);
        return true;
    }
    return false;
}

template <>
template <>
inline bool Watcher<ev_signal>::setup<int>(int signum) {
    if (active())
        cancel();
    if (signum > 0) {
        ev_signal_set(&ev, signum);
        ev_signal_start(p_loop, &ev);
        return active();
    }
    return false;
}

using TimerWatcher = Watcher<ev_timer>;
using SignalWatcher = Watcher<ev_signal>;

class IoWatcher : public Watcher<ev_io> {

    template <typename T>
    friend class Dispatcher;

  public:
    /* File descriptor */
    int fd() noexcept {
        return ev.fd;
    }

    /* Current watching mode */
    int mode() noexcept {
        return ev.events & (EV_READ | EV_WRITE);
    }

    /* Constructor */
    IoWatcher(OnEvent&& on_event, const std::shared_ptr<PlatformLoop>& sp_loop)
        : Watcher<ev_io>(std::forward<OnEvent>(on_event), sp_loop) {}

};
}
#endif // SQUALL__PLATFORM_WATCHERS_HXX
