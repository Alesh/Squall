#ifndef SQUALL__CORE__BUFFERS_HXX
#define SQUALL__CORE__BUFFERS_HXX
#include <vector>
#include <algorithm>
#include <functional>
#include <cassert>
#include "Exceptions.hxx"
#include "NonCopyable.hxx"
#include "PlatformLoop.hxx"

namespace squall {
namespace core {


/* Base I/O buffer. */
class BaseBuffer : NonCopyable {
  public:
    /* Flow controller interface */
    using FlowCtrl = std::function<bool(bool)>;

    /* Return true if event watching active */
    bool active() const noexcept {
        return bool(on_event);
    }

    /* Return true if buffer flow operations active */
    bool running() const noexcept {
        return !paused;
    }

    /* Returns current buffer size. */
    size_t size() const noexcept {
        return buff.size();
    }

    /* Calculated buffer task result */
    virtual intptr_t lastResult() const noexcept = 0;

    /* Returns last error code */
    int lastError() const noexcept {
        return last_error;
    }

    /* Cancels buffer task. */
    void cancel() noexcept {
        on_event = nullptr;
    }

    /* Release buffer. */
    void cleanup() noexcept {
        if (on_event)
            on_event(Event::CLEANUP, (void*)this);
        cancel();
        buff.clear();
    }

  protected:
    OnEvent on_event;
    FlowCtrl flow_ctrl;
    std::vector<char> buff;
    size_t block_size, max_size;
    bool paused = true;
    int last_error;

    /* Constructor */
    BaseBuffer(FlowCtrl&& flow_ctrl, size_t block_size, size_t max_size)
        : on_event(nullptr), flow_ctrl(std::forward<FlowCtrl>(flow_ctrl)), block_size(block_size),
          max_size(max_size), last_error(0) {
        assert((block_size < max_size) && (block_size % 8 == 0) && (max_size % block_size == 0));
    }

    /* Destructor */
    virtual ~BaseBuffer() {
        cleanup();
    }

    /* resume flow */
    void resume() noexcept {
        if (paused)
            paused = !flow_ctrl(true);
    }

    /* pause flow */
    void pause() noexcept {
        if (!paused)
            paused = flow_ctrl(false);
    }
};


/* Event-driven outcoming buffer. */
class OutcomingBuffer : public BaseBuffer {
  public:
    /* Data transmiter interface */
    using Transmiter = std::function<std::pair<size_t, int>(const char* buff, size_t block_size)>;

    /* Calculated buffer task result */
    intptr_t lastResult() const noexcept {
        if (on_event && (size() <= threshold))
            return 1;
        return 0;
    }

    /* Setup buffer task */
    intptr_t setup(OnEvent&& on_event, size_t threshold) {
        cancel(); // Cancel previos buffer task
        if (threshold > max_size - block_size)
            threshold = max_size - block_size;
        // setup new buffer task
        this->threshold = threshold;
        this->on_event = std::forward<OnEvent>(on_event);
        auto early_result = lastResult();
        if (!early_result)
            resume();
        return early_result;
    }

    /* Writes data to the outcoming buffer. Returns number of written bytes. */
    size_t write(std::vector<char> data) {
        auto number = max_size - size();
        number = (data.size() < number) ? data.size() : number;
        if (number > 0) {
            std::copy(data.begin(), data.begin() + number, std::back_inserter(buff));
            resume();
            return number;
        }
        return 0;
    }

  protected:
    Transmiter transmiter;
    size_t threshold;
    int mode;

    /* Constructor */
    OutcomingBuffer(Transmiter&& transmiter, FlowCtrl&& flow_ctrl, size_t block_size, size_t max_size)
        : BaseBuffer(std::forward<FlowCtrl>(flow_ctrl), block_size, max_size),
          transmiter(std::forward<Transmiter>(transmiter)), threshold(0), mode(Event::WRITE) {
        resume();
    }

    void operator()(int revents) {
        if (revents & (mode | Event::ERROR)) {
            last_error = 0;
            if (revents == mode) {
                revents = 0;
                auto number = block_size < size() ? block_size : size();
                if (number > 0) {
                    auto transmiter_result = transmiter(&(*buff.begin()), number);
                    if (transmiter_result.first > 0) {
                        buff.erase(buff.begin(), buff.begin() + transmiter_result.first);
                    } else {
                        revents = Event::BUFFER | Event::ERROR;
                        if (transmiter_result.second > 0)
                            last_error = transmiter_result.second;
                    }
                }
            }
            if ((revents & Event::ERROR) || (size() == 0))
                pause();
            if (on_event) {
                OnEvent callback = on_event;
                if (!((revents & Event::ERROR))) {
                    if (lastResult() > 0)
                        revents = Event::BUFFER | Event::WRITE;
                } else
                    cancel();
                if (revents)
                    callback(revents, (void*)this);
            }
        }
    }
};


/* Event-driven incoming buffer. */
class IncomingBuffer : public BaseBuffer {
  public:
    /* Data receiver interface */
    using Receiver = std::function<std::pair<size_t, int>(char* buff, size_t block_size)>;

    /* Calculated buffer task result */
    intptr_t lastResult() const noexcept {
        if (on_event) {
            if (delimiter.size() > 0) {
                auto found = std::search(buff.begin(), buff.end(), delimiter.begin(), delimiter.end());
                if (found != buff.end()) {
                    auto result = std::distance(buff.begin(), found) + delimiter.size();
                    return (result < threshold) ? result : -1;
                } else {
                    if (size() >= threshold)
                        return -1;
                }
            } else if (size() >= threshold)
                return threshold;
        }
        return 0;
    }

    /* Setup buffer task */
    intptr_t setup(OnEvent&& on_event, const std::vector<char>& delimiter, size_t threshold) {
        cancel(); // Cancel previos buffer task
        threshold = (threshold < max_size) ? threshold : max_size;
        // setup new buffer task
        this->threshold = threshold;
        this->delimiter = delimiter;
        this->on_event = std::forward<OnEvent>(on_event);
        auto early_result = lastResult();
        if (!early_result)
            resume();
        return early_result;
    }

    /* Read bytes from incoming buffer how much is there, but not more `number`. */
    std::vector<char> read(size_t number) {
        std::vector<char> result;
        number = (number < size()) ? number : size();
        if (number > 0) {
            std::copy(buff.begin(), buff.begin() + number, std::back_inserter(result));
            buff.erase(buff.begin(), buff.begin() + number);
            resume();
        }
        return result;
    }

  protected:
    Receiver receiver;
    std::vector<char> delimiter;
    size_t threshold;
    int mode;

    /* Constructor */
    IncomingBuffer(Receiver&& receiver, FlowCtrl&& flow_ctrl, size_t block_size, size_t max_size)
        : BaseBuffer(std::forward<FlowCtrl>(flow_ctrl), block_size, max_size),
          receiver(std::forward<Receiver>(receiver)), threshold(max_size), mode(Event::READ) {
        resume();
    }

    void operator()(int revents) {
        if (revents & (mode | Event::ERROR)) {
            last_error = 0;
            if (revents == mode) {
                revents = 0;
                auto number = max_size - size();
                number = (number < block_size) ? number : block_size;
                if (number > 0) {
                    auto from = size();
                    buff.resize(buff.size() + number);
                    auto receiver_result = receiver(&(*(buff.begin() + from)), number);
                    if (receiver_result.first != number)
                        buff.resize(buff.size() - number + receiver_result.first);
                    if (receiver_result.first == 0) {
                        revents = Event::BUFFER | Event::ERROR;
                        if (receiver_result.second > 0)
                            last_error = receiver_result.second;
                    }
                }
            }
            if ((revents & Event::ERROR) || (size() >= max_size))
                pause();
            if (on_event) {
                OnEvent callback = on_event;
                if (!((revents & Event::ERROR))) {
                    auto result = lastResult();
                    if (result > 0)
                        revents = Event::BUFFER | Event::READ;
                    else if (result < 0)
                        revents = Event::BUFFER | Event::ERROR | Event::READ;
                } else
                    cancel();
                if (revents)
                    callback(revents, (void*)this);
            }
        }
    }
};

} // squall::core
} // squall
#endif // SQUALL__CORE__BUFFERS_HXX