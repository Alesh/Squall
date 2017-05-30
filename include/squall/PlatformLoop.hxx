#ifndef SQUALL__PLATFORM_LOOP_HXX
#define SQUALL__PLATFORM_LOOP_HXX

#include <ev.h>
#include <memory>
#include <functional>
#include "NonCopyable.hxx"

namespace squall {

/* Event codes */
enum Event : int {
    READ = EV_READ,
    WRITE = EV_WRITE,
    TIMEOUT = EV_TIMER,
    SIGNAL = EV_SIGNAL,
    ERROR = EV_ERROR,
    CLEANUP = EV_CLEANUP,
};

/* Event handler */
using OnEvent = std::function<void(int revents, void* payload)>;


/* Platform event Loop */
class PlatformLoop : NonCopyable {

    template <typename A>
    friend class Watcher;

  public:
    /* Return true if is running */
    bool running() const noexcept {
        return running_;
    }

    /* Returns created pointer to new loop */
    static std::shared_ptr<PlatformLoop> createShared(int flag = EVFLAG_AUTO) {
        return std::shared_ptr<PlatformLoop>(new PlatformLoop(flag));
    }

    /* Destructor. */
    ~PlatformLoop() {
        if (!ev_is_default_loop(raw))
            ev_loop_destroy(raw);
    }

    /* Starts event dispatching. */
    void start() {
        running_ = true;
        while (running_) {
            if (!ev_run(raw, EVRUN_ONCE))
                running_ = false;
        }
    }

    /* Stops event dispatching. */
    void stop() noexcept {
        if (running_) {
            running_ = false;
            ev_break(raw, EVBREAK_ONE);
        }
    }

  private:
    struct ev_loop* raw;
    bool running_ = false;

    /* Constructor. */
    PlatformLoop(int flag) {
        if (flag == -1)
            raw = ev_default_loop(EVFLAG_AUTO);
        else
            raw = ev_loop_new(flag);
    }
};
}
#endif // SQUALL__PLATFORM_LOOP_HXX
