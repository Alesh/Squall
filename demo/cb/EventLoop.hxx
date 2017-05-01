#ifndef SQUALL__CB_EVENT_LOOP__HXX
#define SQUALL__CB_EVENT_LOOP__HXX

#include <squall/Dispatcher.hxx>
#include <squall/EventLoop.hxx>

using namespace std::placeholders;
using Callback = std::function<bool(int revents) noexcept>;

namespace squall {
template <>
std::shared_ptr<Callback> Dispatcher<std::shared_ptr<Callback>>::nullable_ctx() noexcept {
    return std::shared_ptr<Callback>();
}
}


/**
 * Event loop/dispatcher implementation  for callback.
 */
class EventLoop : squall::Dispatcher<std::shared_ptr<Callback>> {
    using Base = squall::Dispatcher<std::shared_ptr<Callback>>;

    void _base_on_event(std::shared_ptr<Callback> handle, int revents) noexcept {
        auto callback = *handle.get();
        if (!callback(revents) && is_running()) {
            cancel_io(handle);
            cancel_timer(handle);
            cancel_signal(handle);
        }
    }

  public:
    EventLoop() : Base(std::bind(&EventLoop::_base_on_event, this, _1, _2), squall::EventLoop::create()) {}

    /* Return true if an event dispatching is active. */
    bool is_running() {
        return Base::shared_loop()->running();
    }

    /* Starts event dispatching.*/
    void start() {
        Base::shared_loop()->start();
    }

    /* Stops event dispatching.*/
    void stop() {
        Base::shared_loop()->stop();
    }

    /* Setup to call `callback` when the I/O device
     * with a given `fd` would be to read and/or write `mode`. */
    std::shared_ptr<Callback> setup_io(const Callback& callback, int fd, int mode) {
        auto handle = std::shared_ptr<Callback>(new Callback(callback));
        if (Base::setup_io(handle, fd, mode))
            return handle;
        else
            return std::shared_ptr<Callback>(nullptr);
    }

    /* Setup to call `callback` every `seconds`. */
    std::shared_ptr<Callback> setup_timer(const Callback& callback, double seconds) {
        auto handle = std::shared_ptr<Callback>(new Callback(callback));
        if (Base::setup_timer(handle, seconds))
            return handle;
        else
            return std::shared_ptr<Callback>(nullptr);
    }

    /* Setup to call `callback` when the system signal
     * with a given `signum` recieved. */
    std::shared_ptr<Callback> setup_signal(const Callback& callback, int signum) {
        auto handle = std::shared_ptr<Callback>(new Callback(callback));
        if (Base::setup_signal(handle, signum))
            return handle;
        else
            return std::shared_ptr<Callback>(nullptr);
    }

    /* Updates I/O mode for event watchig extablished with method `setup_io`. */
    bool update_io(std::shared_ptr<Callback> handle, int mode) {
        return Base::update_io(handle, mode);
    }

    /* Cancels event watchig extablished with method `setup_io`. */
    bool cancel_io(std::shared_ptr<Callback> handle) {
        return Base::cancel_io(handle);
    }

    /* Cancels event watchig extablished with method `setup_timer`. */
    bool cancel_timer(std::shared_ptr<Callback> handle) {
        return Base::cancel_timer(handle);
    }

    /* Cancels event watchig extablished with method `setup_signal`. */
    bool cancel_signal(std::shared_ptr<Callback> handle) {
        return Base::cancel_signal(handle);
    }
};

#endif
