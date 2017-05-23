#ifndef SQUALL__EVENT_BUFFER_HXX
#define SQUALL__EVENT_BUFFER_HXX

#include <vector>
#include <algorithm>
#include "NonCopyable.hxx"
#include "PlatformLoop.hxx"
#include "PlatformWatcher.hxx"

#ifdef HAVE_UNISTD_H
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif // HAVE_UNISTD_H

using std::placeholders::_1;


namespace squall {

/* Event-driven I/O buffer. */
class EventBuffer : public IoWatcher, NonCopyable {
    using watcher = IoWatcher;

    template <typename T>
    friend class Dispatcher;

  public:
    /** Buffer block size */
    size_t blockSize() noexcept {
        return block_size;
    }

    /** Maximum buffer size */
    size_t bufferSize() noexcept {
        return buffer_size;
    }

    /** Incomming buffer size */
    size_t incomingSize() noexcept {
        return in.size();
    }

    /** Outcomming buffer size */
    size_t outcomingSize() noexcept {
        return out.size();
    }

    /* Returns `Event::Read` or `Event::Write` if there is waiting a buffer events. otherwise 0.  */
    int pendingEvent() {
        return pending_event;
    }

    /* Destructor */
    ~EventBuffer() {}

    /** Sets up a buffer for watching receiver events */
    int setup_receiver(size_t threshold) {
        return setup_buffer(Event::Read, threshold, std::vector<char>());
    }

    /** Sets up a buffer for watching receiver events */
    int setup_receiver(const std::vector<char>& delimiter, size_t threshold = 0) {
        return setup_buffer(Event::Read, threshold, delimiter);
    }

    /** Sets up a buffer for watching transmiter events */
    int setup_transmiter(size_t threshold = 0) {
        return setup_buffer(Event::Write, threshold, std::vector<char>());
    }

    /**
     * Read bytes from incoming buffer how much is there, but not more `size`.
     * If `size == 0` tries to read bytes block defined with used threshold
     * and/or delimiter.
     */
    std::vector<char> read(size_t size = 0) {
        std::vector<char> result;
        size = (size < in.size()) ? size : in.size();
        if (pending_event == Event::Read) {
            size = ((size == 0) && (threshold < in.size())) ? threshold : size;
            cancel();
        }
        if (size > 0) {
            std::copy(in.begin(), in.begin() + size, std::back_inserter(result));
            in.erase(in.begin(), in.begin() + size);
            watcher::setup(fd(), mode() | Event::Read);
        }
        return result;
    }

    /** Writes data to the outcoming buffer. Returns number of written bytes. */
    size_t write(std::vector<char> data) {
        if (pending_event == Event::Write)
            cancel();
        auto size = (data.size() < buffer_size - out.size()) ? data.size() : buffer_size - out.size();
        std::copy(data.begin(), data.begin() + size, std::back_inserter(out));
        watcher::setup(fd(), mode() | Event::Write);
        return size;
    }

    /* Cancels a buffer event watching */
    bool cancel() {
        if (pending_event) {
            threshold = 0;
            delimiter.clear();
            pending_event = 0;
            return true;
        }
        return false;
    }

protected:
    /* Constructor */
    EventBuffer(OnEvent&& on_event, const std::shared_ptr<PlatformLoop>& sp_loop, int fd,
                size_t block_size = 0, size_t buffer_size = 0)
        : IoWatcher(std::bind(&EventBuffer::event_handler, this, _1), sp_loop),
          on_event(std::forward<OnEvent>(on_event)), block_size(block_size), buffer_size(buffer_size) {
        watcher::setup(fd, static_cast<int>(Event::Read));
        adjust_buffer_size();
        set_non_block(fd);
    }

    /* Return true if is buffer watcher */
    virtual bool is_buffer() noexcept {
        return true;
    }

#ifndef _POSIX_VERSION
public:
    /** Returns last error code. */
    virtual int lastErrno() noexcept {
        return last_errno;
    };

  protected:
    virtual bool set_non_block(int fd);
    virtual std::pair<size_t, int> write_block(char* buff, size_t block_size) = 0;
    virtual std::pair<size_t, int> read_block(const char* buff, size_t block_size) = 0;

#else
public:
    /** Returns last error code. */
    virtual int lastErrno() noexcept {
        return last_errno >= 0 ? last_errno : EIO;
    };

  protected:
    /* Sets non block */
    virtual bool set_non_block(int fd) {
        auto flags = fcntl(fd, F_GETFL, 0);
        flags = (flags == -1) ? 0 : flags;
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
            last_errno = errno;
        return (last_errno == 0);
    }

    /* Reads block of bytes from device. */
    std::pair<size_t, int> read_block(char* buff, size_t block_size) {
        auto size = ::read(fd(), buff, block_size);
        if (size > 0)
            return std::make_pair(size, 0);
        else if (size == 0)
            return std::make_pair(0, ECONNRESET);
        else if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
            return std::make_pair(0, 0);
        else
            return std::make_pair(0, errno);
    }

    /* Write block of bytes to device. */
    std::pair<size_t, int> write_block(const char* buff, size_t block_size) {
        auto size = ::write(fd(), buff, block_size);
        if (size >= 0)
            return std::make_pair(size, 0);
        else if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
            return std::make_pair(0, 0);
        else
            return std::make_pair(0, errno);
    }
#endif

  private:
    OnEvent on_event;
    int last_errno = 0;
    std::vector<char> in;
    std::vector<char> out;
    int pending_event = 0;
    std::vector<char> delimiter;
    size_t block_size, buffer_size, threshold;

    using watcher::setup;

    int setup_buffer(int pending_event_, size_t threshold_, const std::vector<char>& delimiter_) {
        cancel();
        if (pending_event_ == Event::Read)
            threshold = threshold_ < buffer_size ? threshold_ : buffer_size;
        else if (pending_event_ == Event::Write)
            threshold = threshold_ < (buffer_size - block_size) ? threshold_ : (buffer_size - block_size);
        else {
            return 0;
        }
        delimiter = delimiter_;
        if (!delimiter.empty() && (threshold == 0))
            threshold = buffer_size;
        pending_event = pending_event_;
        threshold = threshold > 0 ? threshold : 0;
        auto early_events = check_buffer_task();
        if (early_events == 0) {
            watcher::setup(fd(), mode() | pending_event);
            return -1;
        } else
            return early_events;
    }

    int check_buffer_task() {
        int result = 0;
        if (pending_event == Event::Read) {
            if (delimiter.size() > 0) {
                auto found = std::search(in.begin(), in.end(), delimiter.begin(), delimiter.end());
                if (found == in.end()) {
                    if (threshold <= incomingSize())
                        // delimiter not found but max_size
                        result = Event::Error;
                } else
                    // delimiter found; move threshold
                    threshold = std::distance(in.begin(), found) + delimiter.size();
            }
            if (threshold <= incomingSize())
                result |= Event::Read;
        } else if (pending_event == Event::Write) {
            if (out.size() <= threshold)
                result = Event::Write;
        }
        if ((result==0)&&(last_errno != 0))
            result = Event::Error;
        return result;
    }

    void adjust_buffer_size() {
        block_size = (block_size > 1024) ? block_size : 1024;
        block_size += (block_size % 1024) ? 1024 : 0;
        block_size = (block_size / 1024) * 1024;
        buffer_size = (buffer_size > 1024 * 4) ? buffer_size : 1024 * 4;
        buffer_size = (buffer_size / block_size) * block_size;
        threshold = 0;
    }

    void event_handler(int revents) {
        if (revents & Event::Error)
            last_errno = -1;
        else {
            size_t from = 0;
            size_t size = 0;
            last_errno = 0;
            if (revents & Event::Read) {
                size = buffer_size - in.size();
                size = (size < block_size) ? size : block_size;
                if (size > 0) {
                    from = in.size();
                    in.resize(in.size() + size);
                    auto result = read_block(&(*(in.begin() + from)), size);
                    if (result.first > 0) {
                        if (result.first != size)
                            in.resize(in.size() - size + result.first);
                    } else
                        last_errno = result.second; // read error
                }
            }
            if ((last_errno == 0) && (revents & Event::Write)) {
                size = out.size() < block_size ? out.size() : block_size;
                if (size > 0) {
                    auto result = write_block(&(*out.begin()), size);
                    if (result.first > 0)
                        out.erase(out.begin(), out.begin() + result.first);
                    else
                        last_errno = result.second; // write error
                }
            }
        }
        // update I/O buffer mode
        if (last_errno == 0) {
            if ((revents & Event::Read) && (in.size() >= buffer_size))
                watcher::setup(fd(), mode() & Event::Write);
            if ((revents & Event::Write) && out.empty())
                watcher::setup(fd(), mode() & Event::Read);
        } else
            watcher::setup(fd(), 0);
        // generate buffer events
        if (pending_event) {
            auto buffer_events = check_buffer_task();
            if (buffer_events)
                on_event(buffer_events);
        }
    }
};
}
#endif // SQUALL__EVENT_BUFFER_HXX