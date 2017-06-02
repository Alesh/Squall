#ifndef SQUALL__EVENT_LOOP__HXX
#define SQUALL__EVENT_LOOP__HXX
#include <squall/core/Dispatcher.hxx>
#include <squall/core/NonCopyable.hxx>
#include <squall/core/PlatformLoop.hxx>

using std::placeholders::_1;
using std::placeholders::_2;

namespace squall {

using core::Event;


/**
 * Event loop implementation  for callback.
 */
class EventLoop : core::NonCopyable {
  public:
    using Callback = std::function<void(int revents) noexcept>;
    using Handle = const std::shared_ptr<Callback>;

    /** Returns true if this dispatcher is active. */
    bool active() noexcept {
        return (sp_loop.get() != nullptr);
    };

    /** Return true if an event dispatching is active. */
    bool running() {
        return sp_loop->running();
    }

    /** Constructor */
    EventLoop()
        : sp_loop(core::PlatformLoop::createShared()),
          dispatcher(std::bind(&EventLoop::on_event, this, _1, _2), sp_loop) {}

    /** Destructor */
    virtual ~EventLoop() {
        release();
    }

    /** Starts event dispatching.*/
    void start() {
        if (active())
            sp_loop->start();
    }

    /** Stops event dispatching.*/
    void stop() {
        if (active())
            sp_loop->stop();
    }

    /** Releases all associated resources. */
    void release() {
        if (active()) {
            ////
            dispatcher.release();
            sp_loop.reset();
        }
    }


    /**
     * Setup to call `callback` when the I/O device
     * with a given `fd` would be to read and/or write `mode`.
     */
    Handle setupIoWatching(Callback&& callback, int fd, int mode) {
        if (active()) {
            auto handle = std::shared_ptr<Callback>(new Callback(std::forward<Callback>(callback)));
            dispatcher.setupIoWatching(handle, fd, mode);
            return handle;
        }
        throw exc::CannotSetupWatching();
    }

    /**
     * Updates I/O mode for event watchig established
     * with method `setupIoWatching` for a given `handle`.
     */
    bool updateIoWatching(Handle& handle, int mode) {
        if (active())
            return dispatcher.updateIoWatching(handle, mode);
        return false;
    }

    /**
     * Cancels an event watchig established
     * with method `setupIoWatching` for a given `handle`.
     */
    bool cancelIoWatching(Handle& handle) {
        if (active())
            return dispatcher.cancelIoWatching(handle);
        return false;
    }

    /** Setup to call `callback` every `seconds`. */
    Handle setupTimerWatching(Callback&& callback, double seconds) {
        if (active()) {
            auto handle = std::shared_ptr<Callback>(new Callback(std::forward<Callback>(callback)));
            dispatcher.setupTimerWatching(handle, seconds);
            return handle;
        }
        throw exc::CannotSetupWatching();
    }

    /**
     * Cancels an event watchig established
     * with method `setupTimerWatching` for a given `handle`.
     */
    bool cancelTimerWatching(Handle& handle) {
        if (active())
            return dispatcher.cancelTimerWatching(handle);
        return false;
    }

    /**
     * Setup to call `callback` after `seconds`
     */
    Handle setupTimeoutWatching(Callback&& callback, double seconds) {
        auto wrapped = Callback(std::forward<Callback>(callback));
        Handle handle = setupTimerWatching(
            [this, &handle, wrapped](int revents) {
                this->cancelTimerWatching(handle);
                wrapped(revents);
            },
            seconds);
        return handle;
    }

    /**
     * Cancels an event watchig established
     * with method `setupTimeoutWatching` for a given `handle`.
     */
    bool cancelTimeoutWatching(Handle& handle) {
        return dispatcher.cancelTimerWatching(handle);
    }

    /**
     * Setup to call `callback` when
     * the system signal with a given `signum` recieved.
     */
    Handle setupSignalWatching(Callback&& callback, int signum) {
        if (active()) {
            auto handle = std::shared_ptr<Callback>(new Callback(std::forward<Callback>(callback)));
            dispatcher.setupSignalWatching(handle, signum);
            return handle;
        }
        throw exc::CannotSetupWatching();
    }


    /**
     * Cancels an event watchig established
     * with method `setupSignalWatching` for a given `handle`.
     */
    bool cancelSignalWatching(Handle& handle) {
        if (active())
            return dispatcher.cancelSignalWatching(handle);
        return false;
    }

  private:
    std::shared_ptr<core::PlatformLoop> sp_loop;
    core::Dispatcher<std::shared_ptr<Callback>> dispatcher;

    void on_event(std::shared_ptr<Callback> handle, int revents) {
        auto callback = *handle.get();
        callback(revents);
    }
};
}
#endif