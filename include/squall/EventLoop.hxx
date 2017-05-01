#ifndef SQUALL__EVENT_LOOP_HXX
#define SQUALL__EVENT_LOOP_HXX

#include <ev.h>
#include <memory>
#include "NonCopyable.hxx"

namespace squall {

/* Event codes */
enum Event : int {
    Read = EV_READ,
    Write = EV_WRITE,
    Timeout = EV_TIMER,
    Signal = EV_SIGNAL,
    Error = EV_ERROR,
    Cleanup = EV_CLEANUP,
};


/* Event Loop */
class EventLoop : NonCopyable {
  public:
    /* Return true if is active */
    bool running() const noexcept {
        return m_running;
    }

    /* Raw pointer to platform event loop */
    struct ev_loop* raw() const noexcept {
        return m_raw;
    }

    /* Returns created pointer to new loop */
    static std::shared_ptr<EventLoop> create(int flag = EVFLAG_AUTO) {
        return std::shared_ptr<EventLoop>(new EventLoop(flag));
    }

    /* Destructor. */
    ~EventLoop() {
        if (!ev_is_default_loop(m_raw))
            ev_loop_destroy(m_raw);
    }

    /* Starts event dispatching. */
    void start() {
        m_running = true;
        while (m_running) {
            if (!ev_run(m_raw, EVRUN_ONCE))
                m_running = false;
        }
    }

    /* Stops event dispatching. */
    void stop() noexcept {
        if (m_running) {
            m_running = false;
            ev_break(m_raw, EVBREAK_ONE);
        }
    }

  private:
    struct ev_loop* m_raw;
    bool m_running = false;

    /* Constructor. */
    EventLoop(int flag) {
        if (flag == -1)
            m_raw = ev_default_loop(EVFLAG_AUTO);
        else
            m_raw = ev_loop_new(flag);
    }
};
}
#endif // SQUALL__EVENT_LOOP_HXX
