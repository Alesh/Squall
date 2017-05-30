#ifndef SQUALL__BUFFERS_HXX
#define SQUALL__BUFFERS_HXX
#include <vector>
#include <algorithm>
#include <functional>
#include <cassert>
#include "NonCopyable.hxx"
#include "PlatformLoop.hxx"
#include "PlatformWatchers.hxx"

using std::placeholders::_1;
using std::placeholders::_2;

namespace squall {


/* Event-driven base buffer. */
class EventBuffer : NonCopyable {
    template <typename T>
    friend class Dispatcher;

  public:
    /* File descriptor. */
    int fd() const noexcept {
        return watcher.fd();
    }

    /* Returns true if buffer is active. */
    bool active() const noexcept {
        return active_;
    }

    /* Returns true if buffer is running (filling or flushing). */
    bool running() const noexcept {
        return watcher.running();
    }

    /* Returns true if buffer in awaiting buffer event. */
    bool awaiting() const noexcept {
        return bool(on_event_);
    }

    /* Returns current buffer size. */
    size_t size() const noexcept {
        return buff.size();
    }

    /* Returns buffer block size. */
    size_t blockSize() const noexcept {
        return block_size;
    }

    /* Returns maximum buffer size. */
    size_t maxSize() const noexcept {
        return max_size;
    }

    /* Cancels buffer task. */
    virtual void cancel() noexcept {
        on_event_ = nullptr;
    };

    /* Returns last error code */
    intptr_t lastError() const noexcept {
        return last_error;
    }

    /* Calculated buffer task result */
    virtual intptr_t lastResult() const noexcept = 0;

  protected:
    OnEvent on_event_;
    std::vector<char> buff;
    IoWatcher watcher;
    size_t block_size, max_size;
    intptr_t last_error;
    bool active_;
    int mode;

    /* Constructor */
    EventBuffer(const std::shared_ptr<PlatformLoop>& sp_loop, int fd, int mode, size_t block_size,
                size_t max_size)
        : watcher(std::bind(&EventBuffer::event_handler, this, _1, _2), sp_loop), block_size(block_size),
          max_size(max_size), last_error(0), active_(true), mode(mode) {
        assert((block_size < max_size) && (block_size % 8 == 0) && (max_size % block_size == 0));
        watcher.setup(fd, mode);
    }

    virtual ~EventBuffer() {
        release();
    }

    /* Release buffer. */
    void release() noexcept {
        if (active_) {
            if (on_event_)
                on_event_(Event::CLEANUP, (void*)this);
            cancel();
            buff.clear();
            watcher.cancel();
            active_ = false;
        }
    }

    /* Pause buffer operations */
    void pause() {
        if (watcher.mode() == mode)
            watcher.setup(fd(), 0);
    }

    /* Pause resume operations */
    void resume() {
        if (watcher.mode() == 0)
            watcher.setup(fd(), mode);
    }

    virtual int process_buffer(int revents) = 0;
    virtual int process_task(int revents) = 0;

    /* Event handler */
    void event_handler(int revents, void* payload) {
        if (revents & (mode | Event::ERROR)) {
            last_error = 0;
            if (revents == mode) {
                revents = process_buffer(revents);
            } else
                revents = Event::ERROR;

            if (revents & Event::ERROR)
                pause(); // buffer filling/flushing would be paused after error

            if (on_event_) {
                auto on_event = on_event_;
                if (!(revents & Event::ERROR))
                    revents = process_task(revents);
                else
                    cancel();
                if (revents != 0)
                    on_event(revents, (void*)this);
            }
        }
    }
};


/* Event-driven outcoming buffer. */
class OutcomingBuffer : public EventBuffer {
    template <typename T>
    friend class Dispatcher;

  public:
    /* Data transmiter interface */
    using Transmiter = std::function<std::pair<size_t, int>(const char* buff, size_t block_size)>;

    /* Calculated buffer task result */
    intptr_t lastResult() const noexcept {
        if (on_event_ && (size() <= threshold_))
            return 1;
        return 0;
    }

    /* Setup buffer task */
    intptr_t setup(OnEvent&& on_event, size_t threshold) {
        cancel(); // Cancel previos buffer task
        if (threshold > maxSize() - blockSize())
            threshold = maxSize() - blockSize();
        // setup new buffer task
        threshold_ = threshold;
        on_event_ = std::forward<OnEvent>(on_event);
        return lastResult();
    }

    /* Cancels buffer task. */
    void cancel() noexcept {
        threshold_ = 0;
        EventBuffer::cancel();
    }

    /* Writes data to the outcoming buffer. Returns number of written bytes. */
    size_t write(std::vector<char> data) {
        if (active()) {
            auto number = maxSize() - size();
            number = (data.size() < number) ? data.size() : number;
            if (number > 0) {
                std::copy(data.begin(), data.begin() + number, std::back_inserter(buff));
                resume(); // runs buffer if it paused
            }
            return number;
        }
        return 0;
    }

  protected:
    Transmiter transmiter;
    size_t threshold_;

    /* Constructor */
    OutcomingBuffer(const std::shared_ptr<PlatformLoop>& sp_loop, Transmiter transmiter, int fd,
                    size_t block_size, size_t max_size)
        : EventBuffer(sp_loop, fd, static_cast<int>(Event::WRITE), block_size, max_size),
          transmiter(std::forward<Transmiter>(transmiter)), threshold_(0) {
        resume();
    }

    /* Flushing buffer */
    virtual int process_buffer(int revents) {
        revents = 0;
        auto number = blockSize() < size() ? blockSize() : size();
        if (number > 0) {
            auto transmiter_result = transmiter(&(*buff.begin()), number);
            if (transmiter_result.first > 0) {
                buff.erase(buff.begin(), buff.begin() + transmiter_result.first);
            } else {
                if (transmiter_result.second > 0) {
                    last_error = transmiter_result.second;
                    revents = Event::BUFFER | Event::ERROR;
                }
            }
        } else
            pause();
        return revents;
    }

    /* Process buffer task */
    virtual int process_task(int revents) {
        revents = 0;
        if (lastResult() == 1)
            revents = Event::BUFFER | Event::WRITE;
        return revents;
    }
};
}
#endif // SQUALL__BUFFERS_HXX