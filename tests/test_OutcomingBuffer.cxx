#include <string>
#include <memory>
#include <iostream>
#include <squall/Buffers.hxx>
#include <squall/Dispatcher.hxx>
#include <squall/PlatformLoop.hxx>
#include "catch.hpp"

using squall::Event;
using squall::PlatformLoop;
using squall::OutcomingBuffer;
using squall::Dispatcher;


enum : intptr_t { MARK = -1, HANDLER = -2, TRANSMITER = -3, TRANSMITER_ERR = -4 };


inline std::vector<char> cnv(const std::string& str) {
    return std::vector<char>(str.begin(), str.end());
}


class OutcomingBufferTest : public OutcomingBuffer {
    int buffer_error;
    size_t apply_size;
    std::vector<intptr_t>& callog_;

    std::pair<size_t, int> transmiter(const char* buff, size_t block_size) {
        if (buffer_error == 0) {
            callog_.push_back(TRANSMITER);
            callog_.push_back(block_size);
            auto transmited = (apply_size < blockSize()) && (apply_size > 0) ? apply_size : blockSize();
            transmited = block_size < transmited ? block_size : transmited;
            callog_.push_back(transmited);
            return std::make_pair(transmited, 0);
        } else {
            callog_.push_back(TRANSMITER_ERR);
            callog_.push_back(block_size);
            callog_.push_back(buffer_error);
            return std::make_pair(0, buffer_error);
        }
    }

  public:
    /* Constructor */
    OutcomingBufferTest(std::vector<intptr_t>& callog, size_t block_size, size_t max_size)
        : OutcomingBuffer(PlatformLoop::createShared(),
                          std::bind(&OutcomingBufferTest::transmiter, this, _1, _2), -1, block_size,
                          max_size),
          buffer_error(0), apply_size(0), callog_(callog) {}


    void setBufferError(int value) {
        buffer_error = value;
    }

    void setApplySize(int value) {
        value = value < blockSize() ? value : blockSize();
        apply_size = value;
    }

    void triggerEvent(int events) {
        event_handler(events, nullptr);
    }

    using OutcomingBuffer::release;
};


TEST_CASE("Unittest squall::OutcommingBuffer", "[buffer]") {

    std::vector<intptr_t> callog;
    OutcomingBufferTest out(callog, 8, 16);

    auto handler = [&callog](int revents, void* payload) {
        auto p_buff = static_cast<OutcomingBufferTest*>(payload);
        callog.push_back(HANDLER);
        callog.push_back(revents);
        callog.push_back(p_buff->size());
        callog.push_back(p_buff->lastError());
    };
    // #1
    callog.push_back(MARK);
    callog.push_back(1);
    callog.push_back(0);
    REQUIRE(out.active());
    REQUIRE((out.size()) == 0);
    REQUIRE((out.blockSize()) == 8);
    REQUIRE((out.maxSize()) == 16);
    REQUIRE((out.write(cnv("0123456789ABCDEFZZZ")) == 16));
    REQUIRE((out.size()) == 16);
    out.triggerEvent(Event::WRITE);
    REQUIRE((out.size()) == 8);
    out.setApplySize(4);
    out.triggerEvent(Event::WRITE);
    REQUIRE((out.size()) == 4);
    out.triggerEvent(Event::WRITE);
    REQUIRE((out.size()) == 0);

    // #2
    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(0);
    REQUIRE(out.setup(handler, 0) == 1); // early result (buffer size <= 0)
    REQUIRE(out.setup(handler, 8) == 1); // early result (buffer size <= 8)
    REQUIRE((out.write(cnv("0123456789ABCDEFZZZ")) == 16));
    REQUIRE((out.size()) == 16);
    out.triggerEvent(Event::WRITE);
    out.triggerEvent(Event::WRITE);

    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(1);
    out.setApplySize(0);
    REQUIRE((out.size()) == 8);
    REQUIRE(out.setup(handler, 0) == 0); // await flushing (buffer size > 0)
    out.triggerEvent(Event::WRITE);

    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(2);
    REQUIRE(out.awaiting());
    REQUIRE((out.write(cnv("01234")) == 5));
    out.triggerEvent(Event::WRITE);

    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(3);
    out.cancel(); // cancels buffer task; no more call handler
    REQUIRE(!out.awaiting());
    REQUIRE((out.write(cnv("01234")) == 5));
    out.triggerEvent(Event::WRITE);

    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(4);
    REQUIRE((out.write(cnv("012345")) == 6));
    out.triggerEvent(Event::ERROR); // emul. internal event loop error
    REQUIRE(!out.awaiting());       // and nothing awainitg inactive


    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(5);
    REQUIRE((out.size()) == 6);
    REQUIRE(out.setup(handler, 0) == 0); // await flushing (buffer size > 0)
    REQUIRE(out.awaiting());
    out.triggerEvent(Event::ERROR); // emul. internal event loop error
    REQUIRE(!out.awaiting());  // Error cancel task and pause running

    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(6);
    out.setApplySize(4);
    REQUIRE((out.size()) == 6);
    REQUIRE(out.setup(handler, 4) == 0); // await flushing (buffer size > 4)
    out.triggerEvent(Event::WRITE);
    REQUIRE((out.size()) == 2);
    REQUIRE((out.write(cnv("012345")) == 6));
    out.triggerEvent(Event::WRITE);
    REQUIRE((out.size()) == 4);

    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(7);
    out.setBufferError(13);
    out.triggerEvent(Event::WRITE);
    REQUIRE((out.size()) == 4);
    REQUIRE(!out.awaiting());  // Error cancel task and pause running

    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(8);
    out.setBufferError(0);
    REQUIRE(out.setup(handler, 0) == 0); // await flushing (buffer size > 0)
    REQUIRE(out.awaiting()); // new task resume running
    out.triggerEvent(Event::WRITE);
    REQUIRE((out.size()) == 0);

    callog.push_back(MARK);
    callog.push_back(2);
    callog.push_back(9);
    REQUIRE(out.active());
    out.release(); // Done!
    REQUIRE(!out.awaiting());
    REQUIRE(!out.active());


    REQUIRE(callog == std::vector<intptr_t>({
                          // clang-format off
        MARK, 1, 0,
        TRANSMITER, 8, 8,
        TRANSMITER, 8, 4,
        TRANSMITER, 4, 4,

        MARK, 2, 0,
        TRANSMITER, 8, 4,
        TRANSMITER, 8, 4,
        HANDLER, Event::BUFFER|Event::WRITE, 8, 0,

        MARK, 2, 1,
        TRANSMITER, 8, 8,
        HANDLER, Event::BUFFER|Event::WRITE, 0, 0,

        MARK, 2, 2,
        TRANSMITER, 5, 5,
        HANDLER, Event::BUFFER|Event::WRITE, 0, 0,

        MARK, 2, 3,
        TRANSMITER, 5, 5,

        MARK, 2, 4,

        MARK, 2, 5,
        HANDLER, Event::ERROR, 6, 0,   // internal event loop error

        MARK, 2, 6,
        TRANSMITER, 6, 4,
        HANDLER, Event::BUFFER|Event::WRITE, 2, 0,
        TRANSMITER, 8, 4,
        HANDLER, Event::BUFFER|Event::WRITE, 4, 0,

        MARK, 2, 7,
        TRANSMITER_ERR, 4, 13,
        HANDLER, Event::BUFFER|Event::ERROR, 4, 13,


        MARK, 2, 8,
        TRANSMITER, 4, 4,
        HANDLER, Event::BUFFER|Event::WRITE, 0, 0,

        MARK, 2, 9,
        HANDLER, Event::CLEANUP, 0, 0,

                          // clang-format on
                      }));
}