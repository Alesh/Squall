#include <string>
#include <memory>
#include <iostream>
#include <squall/core/Buffers.hxx>
#include "../catch.hpp"

using squall::core::Event;
using squall::core::IncomingBuffer;
using std::placeholders::_1;
using std::placeholders::_2;


enum : intptr_t { MARK = -1, HANDLER = -2, RECEIVER = -3, RECEIVER_ERR = -4, RESUME = -5, PAUSE = -6 };

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

    std::pair<size_t, int> receiver(char* buff, size_t size) {
        if (buffer_error == 0) {
            callog_.push_back(RECEIVER);
            callog_.push_back(size);
            auto received = (temp_buff.size() < block_size) ? temp_buff.size() : block_size;
            received = size < received ? size : received;
            std::copy(temp_buff.begin(), temp_buff.begin() + received, buff);
            temp_buff.erase(temp_buff.begin(), temp_buff.begin() + received);
            callog_.push_back(received);
            return std::make_pair(received, 0);
        } else {
            callog_.push_back(RECEIVER_ERR);
            callog_.push_back(size);
            callog_.push_back(buffer_error);
            return std::make_pair(0, buffer_error);
        }
    }

  public:
    /* Constructor */
    IncomingBufferTest(std::vector<intptr_t>& callog, IncomingBuffer::FlowCtrl&& flow_ctrl, size_t block_size,
                       size_t max_size)
        : IncomingBuffer(std::bind(&IncomingBufferTest::receiver, this, _1, _2),
                         std::forward<FlowCtrl>(flow_ctrl), block_size, max_size),
          buffer_error(0), callog_(callog) {}

    void setBufferError(int value) {
        buffer_error = value;
    }

    void applyData(std::vector<char> data) {
        std::copy(data.begin(), data.end(), std::back_inserter(temp_buff));
    };

    using IncomingBuffer::operator();
    using IncomingBuffer::cleanup;
};


TEST_CASE("Unittest squall::IncommingBuffer", "[buffer]") {

    std::vector<intptr_t> callog;
    std::vector<char> empty_delimiter;

    auto flow_ctrl = [&callog](bool resume) {
        if (resume)
            callog.push_back(RESUME);
        else
            callog.push_back(PAUSE);
        return true;
    };

    IncomingBufferTest in(callog, flow_ctrl, 8, 32);

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
    callog.push_back(1000);
    REQUIRE(!in.active());
    REQUIRE(in.running());
    REQUIRE((in.size()) == 0);
    in.applyData(cnv("01234567XXX\nXXX\n01234"));
    REQUIRE(in.setup(handler, empty_delimiter, 8) == 0); // await reading (buffer empty)
    REQUIRE(in.active());
    in(Event::READ); // event!



    callog.push_back(MARK);
    callog.push_back(2000);
    REQUIRE((in.size()) == 0);
    REQUIRE(in.setup(handler, cnv("\n"), 100) == 0); // await reading (buffer empty)
    in(Event::READ);
    REQUIRE((in.size()) == 4);
    REQUIRE(in.setup(handler, cnv("\n"), 100) == 4); // early result, bufer has need data
    REQUIRE(cnv(in.read(4)) == "XXX\n");
    REQUIRE((in.size()) == 0);
    in(Event::READ);
    REQUIRE((in.size()) == 5);     // buffer is not empty
    REQUIRE(in.active());
    REQUIRE(in.running());
    REQUIRE(in.lastResult() == 0); // buffer has not requested delimiter

    callog.push_back(MARK);
    callog.push_back(3000);
    in.applyData(cnv("56789\r\n01234567\r\n012"));
    // await reading (buffer is not empty)
    REQUIRE(in.setup(handler, cnv("\r\n"), 64) == 0);
    in(Event::READ);  // event
    REQUIRE((in.size()) == 1);
    in(Event::READ);
    REQUIRE((in.size()) == 9);
    in(Event::READ);   // event
    REQUIRE((in.size()) == 3);
    REQUIRE(in.active());
    REQUIRE(in.running());


    callog.push_back(MARK);
    callog.push_back(4000);
    REQUIRE((in.size()) == 3);
    in.cancel();
    REQUIRE(!in.active());
    REQUIRE(in.running());
    // await reading (buffer is not empty, but absent delimiter, max_size = 32, not 100)
    REQUIRE(in.setup(handler, cnv("\t"), 100) == 0);
    REQUIRE(in.active());
    REQUIRE(in.running());
    in.applyData(cnv("3456789"));
    in.applyData(cnv("0123456789"));
    in.applyData(cnv("0123456789"));
    in.applyData(cnv("0123456789"));
    in(Event::READ);
    in(Event::READ);
    in(Event::READ);
    in(Event::READ);
    REQUIRE(in.setup(handler, cnv("89"), 4) == -1); // early result, delimiter not found but max_size == 4

    callog.push_back(MARK);
    callog.push_back(5000);
    REQUIRE(in.active());
    REQUIRE(!in.running());
    REQUIRE(in.setup(handler, empty_delimiter, 20) == 20);
    REQUIRE(in.lastResult() == 20);
    REQUIRE(cnv(in.read(20)) == "01234567890123456789");
    REQUIRE(in.active());
    REQUIRE(in.running());
    in(Event::ERROR); // emul. internal event loop error
    REQUIRE(!in.active()); // Error cancel task and pause running
    REQUIRE(!in.running());

    callog.push_back(MARK);
    callog.push_back(6000);
    REQUIRE((in.size()) == 12);
    REQUIRE(in.setup(handler, empty_delimiter, 20) == 0);
    in.setBufferError(13); // emul. buffer error
    in(Event::READ);
    REQUIRE(!in.active()); // Error cancel task and pause running
    REQUIRE(!in.running());
    // Buffer is not cleaned when error occurred, u can read it
    REQUIRE(cnv(in.read(6)) == "012345");
    REQUIRE(in.running()); // and this resume reading from device


    callog.push_back(MARK);
    callog.push_back(7000);
    in.setBufferError(0);
    REQUIRE((in.size()) == 6);
    REQUIRE(in.setup(handler, empty_delimiter, 20) == 0);
    in(Event::READ);
    REQUIRE((in.size()) == 14);
    in(Event::READ);  // event buffer EOF
    REQUIRE((in.size()) == 14);
    REQUIRE(!in.active()); // Error cancel task and pause running
    REQUIRE(!in.running());
    REQUIRE(in.read(64) == cnv("67890123456789"));
    REQUIRE((in.size()) == 0);
    REQUIRE(in.running());
    in(Event::READ);
    REQUIRE(!in.running());
    REQUIRE((in.size()) == 0);
    in.cleanup();


    REQUIRE(callog == std::vector<intptr_t>({
                          // clang-format off
        RESUME,
        MARK, 1000,
        RECEIVER, 8, 8,
        HANDLER, Event::BUFFER|Event::READ, 8, 0,


        MARK, 2000,
        RECEIVER, 8, 8,
        HANDLER, Event::BUFFER|Event::READ, 4, 0,
        RECEIVER, 8, 5,

        MARK, 3000,
        RECEIVER, 8, 8,
        HANDLER, Event::BUFFER|Event::READ, 12, 0,
        RECEIVER, 8, 8,
        RECEIVER, 8, 4,
        HANDLER, Event::BUFFER|Event::READ, 10, 0,

        MARK, 4000,
        RECEIVER, 8, 8,
        RECEIVER, 8, 8,
        RECEIVER, 8, 8,
        RECEIVER, 5, 5,
        PAUSE,
        HANDLER, Event::BUFFER|Event::ERROR|Event::READ, -1, 0, // delimiter not found but max_size == 32

        MARK, 5000,
        RESUME,
        PAUSE,
        HANDLER, Event::ERROR, 0, 0,  // internal event loop error


        MARK, 6000,
        RESUME,
        RECEIVER_ERR, 8, 13,
        PAUSE,
        HANDLER, Event::BUFFER|Event::ERROR, 0, 13,  // buffer error with errno 13
        RESUME,

        MARK, 7000,
        RECEIVER, 8, 8,
        RECEIVER, 8, 0,
        PAUSE,
        HANDLER, Event::BUFFER|Event::ERROR, 0, 0,  // buffer EOF or connection reset
        RESUME,
        RECEIVER, 8, 0,
        PAUSE,


                          // clang-format on
                      }));
}