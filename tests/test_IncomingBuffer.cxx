#include <string>
#include <memory>
#include <iostream>
#include <squall/Buffers.hxx>
#include <squall/Dispatcher.hxx>
#include <squall/PlatformLoop.hxx>
#include "catch.hpp"

using squall::Event;
using squall::PlatformLoop;
using squall::IncomingBuffer;
using squall::Dispatcher;


enum : intptr_t { MARK = -1, HANDLER = -2, RECEIVER = -3, RECEIVER_ERR = -4 };


inline std::vector<char> cnv(const std::string& str) {
    return std::vector<char>(str.begin(), str.end());
}

inline std::string cnv(const std::vector<char>& vec) {
    return std::string(vec.begin(), vec.end());
}


class IncomingBufferTest : public IncomingBuffer {
    int buffer_error;
    std::vector<char> temp_buff;
    std::vector<intptr_t>& callog_;

    std::pair<size_t, int> receiver(char* buff, size_t block_size) {

        if (buffer_error == 0) {
            callog_.push_back(RECEIVER);
            callog_.push_back(block_size);
            auto received = (temp_buff.size() < blockSize()) ? temp_buff.size() : blockSize();
            received = block_size < received ? block_size : received;
            std::copy(temp_buff.begin(), temp_buff.begin() + received, buff);
            temp_buff.erase(temp_buff.begin(), temp_buff.begin() + received);
            callog_.push_back(received);
            return std::make_pair(received, 0);
        } else {
            callog_.push_back(RECEIVER_ERR);
            callog_.push_back(block_size);
            callog_.push_back(buffer_error);
            return std::make_pair(0, buffer_error);
        }
    }


  public:
    /* Constructor */
    IncomingBufferTest(std::vector<intptr_t>& callog, size_t block_size, size_t max_size)
        : IncomingBuffer(PlatformLoop::createShared(), std::bind(&IncomingBufferTest::receiver, this, _1, _2),
                         -1, block_size, max_size),
          buffer_error(0), callog_(callog) {}

    void setBufferError(int value) {
        buffer_error = value;
    }

    void triggerEvent(int events) {
        event_handler(events, nullptr);
    }

    void applyData(std::vector<char> data) {
        std::copy(data.begin(), data.end(), std::back_inserter(temp_buff));
    };

    using IncomingBuffer::release;
};


TEST_CASE("Unittest squall::IncommingBuffer", "[buffer]") {

    std::vector<intptr_t> callog;
    std::vector<char> empty_delimiter;
    IncomingBufferTest in(callog, 8, 32);

    auto handler = [&callog](int revents, void* payload) {
        auto p_buff = static_cast<IncomingBufferTest*>(payload);
        callog.push_back(HANDLER);
        callog.push_back(revents);
        auto last_result = p_buff->lastResult();
        if (last_result > 0)
            callog.push_back((p_buff->read(last_result)).size());
        else
            callog.push_back(last_result);
        callog.push_back(p_buff->lastError());
    };


    callog.push_back(MARK);
    callog.push_back(0);
    REQUIRE(in.active());
    REQUIRE((in.size()) == 0);
    REQUIRE((in.blockSize()) == 8);
    REQUIRE((in.maxSize()) == 32);
    in.applyData(cnv("01234567XXX\nXXX\n01234"));
    REQUIRE(in.setup(handler, empty_delimiter, 8) == 0); // await reading (buffer empty)
    in.triggerEvent(Event::READ);

    callog.push_back(MARK);
    callog.push_back(1);
    REQUIRE((in.size()) == 0);
    REQUIRE(in.setup(handler, cnv("\n"), 100) == 0); // await reading (buffer empty)
    in.triggerEvent(Event::READ);
    REQUIRE((in.size()) == 4);
    REQUIRE(in.setup(handler, cnv("\n"), 100) == 4); // early result, bufer has need data
    REQUIRE(cnv(in.read(4)) == "XXX\n");
    REQUIRE((in.size()) == 0);
    in.triggerEvent(Event::READ);
    REQUIRE((in.size()) == 5);  // buffer is not empty
    REQUIRE(in.awaiting());     // buffer is active
    REQUIRE(in.lastResult() == 0);  // buffer has not requested delimiter

    callog.push_back(MARK);
    callog.push_back(2);
    in.applyData(cnv("56789\r\n"));
    in.applyData(cnv("01234567\r\n012"));
    REQUIRE(in.setup(handler, cnv("\r\n"), 100) == 0); // await reading (buffer is not empty, but absent delimiter)
    in.triggerEvent(Event::READ);
    in.triggerEvent(Event::READ);
    in.triggerEvent(Event::READ);

    callog.push_back(MARK);
    callog.push_back(3);
    REQUIRE((in.size()) == 3);
    REQUIRE(in.awaiting());
    in.cancel();
    REQUIRE(!in.awaiting());
    REQUIRE(in.setup(handler, cnv("\t"), 100) == 0); // await reading (buffer is not empty, but absent delimiter)
    REQUIRE(in.awaiting());
    in.applyData(cnv("3456789"));
    in.applyData(cnv("0123456789"));
    in.applyData(cnv("0123456789"));
    in.applyData(cnv("0123456789"));
    in.triggerEvent(Event::READ);
    in.triggerEvent(Event::READ);
    in.triggerEvent(Event::READ);
    in.triggerEvent(Event::READ);
    REQUIRE(in.setup(handler, cnv("89"), 4) == -1); // early result, delimiter not found but max_size == 4

    callog.push_back(MARK);
    callog.push_back(4);
    REQUIRE(in.setup(handler, empty_delimiter, 20) == 20);
    REQUIRE(in.lastResult() == 20);
    REQUIRE(cnv(in.read(20)) == "01234567890123456789");
    REQUIRE(in.awaiting());
    in.triggerEvent(Event::ERROR); // emul. internal event loop error
    REQUIRE(!in.awaiting());  // Error cancel task and pause running

    callog.push_back(MARK);
    callog.push_back(5);
    REQUIRE((in.size()) == 12);
    REQUIRE(in.setup(handler, empty_delimiter, 20) == 0);
    in.setBufferError(13);  // emul. buffer error
    in.triggerEvent(Event::READ);
    REQUIRE(!in.awaiting());  // Error cancel task and pause running
    REQUIRE(cnv(in.read(6)) == "012345");
    // U can read 6 chars, buffer already has need data
    // Buffer is not cleaned when error occurred

    callog.push_back(MARK);
    callog.push_back(6);
    in.setBufferError(0);
    REQUIRE(in.setup(handler, empty_delimiter, 20) == 0);
    in.triggerEvent(Event::READ);
    in.triggerEvent(Event::READ);

    callog.push_back(MARK);
    callog.push_back(7);
    REQUIRE(!in.awaiting());  // Error cancel task and pause running
    in.release(); // Done!
    REQUIRE(!in.active());



    REQUIRE(callog == std::vector<intptr_t>({
                          // clang-format off
        MARK, 0,
        RECEIVER, 8, 8,
        HANDLER, Event::BUFFER|Event::READ, 8, 0,

        MARK, 1,
        RECEIVER, 8, 8,
        HANDLER, Event::BUFFER|Event::READ, 4, 0,
        RECEIVER, 8, 5,

        MARK, 2,
        RECEIVER, 8, 8,
        HANDLER, Event::BUFFER|Event::READ, 12, 0,
        RECEIVER, 8, 8,
        RECEIVER, 8, 4,
        HANDLER, Event::BUFFER|Event::READ, 10, 0,

        MARK, 3,
        RECEIVER, 8, 8,
        RECEIVER, 8, 8,
        RECEIVER, 8, 8,
        RECEIVER, 5, 5,  // Buffer full
        HANDLER, Event::BUFFER|Event::ERROR|Event::READ, -1, 0,
        // delimiter not found!!! max_size == 100, but max buffer size == 32

        MARK, 4,
        HANDLER, Event::ERROR, 0, 0,  // Event loop internal error

        MARK, 5,
        RECEIVER_ERR, 8, 13,
        HANDLER, Event::BUFFER|Event::ERROR, 0, 13,  // Buffer error; errno == 13

        MARK, 6,
        RECEIVER, 8, 8,
        HANDLER, Event::BUFFER|Event::READ, 20, 0,
        RECEIVER, 8, 0,
        HANDLER, Event::BUFFER|Event::ERROR, 0, 0,  // EOF or Connection reset

        MARK, 7,

                          // clang-format on
                      }));
}