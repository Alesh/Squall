#include <string>
#include <memory>
#include <iostream>
#include <squall/core/Buffers.hxx>
#include "../catch.hpp"

using squall::core::Event;
using squall::core::OutcomingBuffer;
using std::placeholders::_1;
using std::placeholders::_2;


enum : intptr_t { MARK = -1, HANDLER = -2, TRANSMITER = -3, TRANSMITER_ERR = -4, RESUME = -5, PAUSE = -6 };

inline std::vector<char> cnv(const std::string& str) {
    return std::vector<char>(str.begin(), str.end());
}


class OutcomingBufferTest : public OutcomingBuffer {
    int buffer_error;
    size_t apply_size;
    std::vector<intptr_t>& callog_;

    std::pair<size_t, int> transmiter(const char* buff, size_t size) {
        if (buffer_error == 0) {
            callog_.push_back(TRANSMITER);
            callog_.push_back(size);
            auto transmited = (apply_size < block_size) ? apply_size : block_size;
            transmited = size < transmited ? size : transmited;
            callog_.push_back(transmited);
            return std::make_pair(transmited, 0);
        } else {
            callog_.push_back(TRANSMITER_ERR);
            callog_.push_back(size);
            callog_.push_back(buffer_error);
            return std::make_pair(0, buffer_error);
        }
    }

  public:
    /* Constructor */
    OutcomingBufferTest(std::vector<intptr_t>& callog, OutcomingBuffer::FlowCtrl&& flow_ctrl, size_t block_size, size_t max_size)
        : OutcomingBuffer(std::bind(&OutcomingBufferTest::transmiter, this, _1, _2),
                          std::forward<FlowCtrl>(flow_ctrl), block_size, max_size),
          buffer_error(0), apply_size(max_size), callog_(callog) {}


    void setBufferError(int value) {
        buffer_error = value;
    }

    void setApplySize(size_t value) {
        value = value < block_size ? value : block_size;
        apply_size = value;
    }

    using OutcomingBuffer::operator();
    using OutcomingBuffer::cleanup;
};


TEST_CASE("Unittest squall::core::OutcommingBuffer", "[buffers]") {
    std::vector<intptr_t> callog;

    auto flow_ctrl = [&callog](bool resume) {
        if (resume)
            callog.push_back(RESUME);
        else
            callog.push_back(PAUSE);
        return true;
    };

    OutcomingBufferTest out(callog, flow_ctrl, 8, 16);

    auto handler = [&callog](int revents, void* payload) {
        auto p_buff = static_cast<OutcomingBufferTest*>(payload);
        callog.push_back(HANDLER);
        callog.push_back(revents);
        callog.push_back(p_buff->size());
        callog.push_back(p_buff->lastError());
    };
    callog.push_back(MARK);
    callog.push_back(1000);

    REQUIRE(!out.active());
    REQUIRE(out.running());
    REQUIRE((out.size()) == 0);
    REQUIRE((out.write(cnv("01234567")) == 8));
    REQUIRE(!out.active());
    REQUIRE(out.running());
    REQUIRE((out.size()) == 8);
    REQUIRE((out.write(cnv("89ABCDEFZZZ")) == 8));
    REQUIRE((out.size()) == 16);
    out(Event::WRITE);
    REQUIRE((out.size()) == 8);
    out.setApplySize(4);
    out(Event::WRITE);
    REQUIRE((out.size()) == 4);
    out(Event::WRITE);
    REQUIRE((out.size()) == 0);

    // #2
    callog.push_back(MARK);
    callog.push_back(2000);
    REQUIRE(!out.active());
    REQUIRE(!out.running());
    REQUIRE(out.setup(handler, 0) == 1); // early result (buffer size <= 0)
    REQUIRE(out.setup(handler, 8) == 1); // early result (buffer size <= 8)
    REQUIRE(out.active());
    REQUIRE(!out.running());
    REQUIRE((out.write(cnv("0123456789ABCDEFZZZ")) == 16));
    REQUIRE((out.size()) == 16);
    REQUIRE(out.running());
    out(Event::WRITE);
    REQUIRE((out.size()) == 12);
    out(Event::WRITE);
    REQUIRE((out.size()) == 8);  // event!

    callog.push_back(MARK);
    callog.push_back(3000);
    out.setApplySize(8);
    REQUIRE((out.size()) == 8);
    REQUIRE(out.setup(handler, 0) == 0); // await flushing (buffer size > 0)
    out(Event::WRITE);
    REQUIRE((out.size()) == 0);  // event!
    REQUIRE(!out.running());
    REQUIRE(out.active());

    callog.push_back(MARK);
    callog.push_back(4000);
    REQUIRE((out.write(cnv("01234")) == 5));
    REQUIRE(out.running());
    out(Event::WRITE);
    REQUIRE(!out.running());
    REQUIRE(out.active());

    callog.push_back(MARK);
    callog.push_back(5000);
    REQUIRE(out.active());
    REQUIRE(!out.running());
    out.cancel(); // cancels buffer task; no more call handler
    REQUIRE(!out.running());
    REQUIRE(!out.active());
    REQUIRE((out.write(cnv("01234")) == 5));
    REQUIRE(out.running());
    REQUIRE(!out.active());
    out(Event::WRITE);
    REQUIRE(!out.active());
    REQUIRE(!out.running());

    callog.push_back(MARK);
    callog.push_back(6000);
    REQUIRE(!out.running());
    REQUIRE(!out.active());
    REQUIRE((out.write(cnv("012345")) == 6));
    REQUIRE(!out.active());
    REQUIRE(out.running());
    out(Event::ERROR); // emul. internal event loop error
    REQUIRE(!out.active());
    REQUIRE(!out.running());

    callog.push_back(MARK);
    callog.push_back(7000);
    REQUIRE((out.size()) == 6);
    REQUIRE(out.setup(handler, 0) == 0); // await flushing (buffer size > 0)
    REQUIRE(out.active());
    REQUIRE(out.running());
    out(Event::ERROR); // emul. internal event loop error
    REQUIRE(!out.active()); // Error cancel task and pause running
    REQUIRE(!out.running());

    callog.push_back(MARK);
    callog.push_back(8000);
    out.setApplySize(4);
    REQUIRE((out.size()) == 6);
    REQUIRE(out.setup(handler, 4) == 0); // await flushing (buffer size > 4)
    out(Event::WRITE);
    REQUIRE((out.size()) == 2); // Event!
    REQUIRE((out.write(cnv("012345")) == 6));
    out(Event::WRITE);
    REQUIRE((out.size()) == 4);
    REQUIRE(out.active());
    REQUIRE(out.running());

    callog.push_back(MARK);
    callog.push_back(9000);
    out.setBufferError(13); // emul. buffer writing error
    out(Event::WRITE);
    REQUIRE((out.size()) == 4);
    REQUIRE(!out.active()); // Error cancel task and pause running
    REQUIRE(!out.running());

    callog.push_back(MARK);
    callog.push_back(1000);
    out.setBufferError(0);
    REQUIRE(out.setup(handler, 0) == 0); // await flushing (buffer size > 0)
    REQUIRE(out.running());              // new task resume running
    REQUIRE(out.active());
    out.setApplySize(0);                 // emul/ Connection reset
    out(Event::WRITE);
    REQUIRE((out.size()) == 4);
    REQUIRE(!out.active()); // Error cancel task and pause running
    REQUIRE(!out.running());

    out.cleanup();
    REQUIRE((out.size()) == 0);


    REQUIRE(callog == std::vector<intptr_t>({
                          // clang-format off
        RESUME,
        MARK, 1000,
        TRANSMITER, 8, 8,
        TRANSMITER, 8, 4,
        TRANSMITER, 4, 4,
        PAUSE,

        MARK, 2000,
        RESUME,
        TRANSMITER, 8, 4,
        TRANSMITER, 8, 4,
        HANDLER, Event::BUFFER|Event::WRITE, 8, 0,

        MARK, 3000,
        TRANSMITER, 8, 8,
        PAUSE,
        HANDLER, Event::BUFFER|Event::WRITE, 0, 0,

        MARK, 4000,
        RESUME,
        TRANSMITER, 5, 5,
        PAUSE,
        HANDLER, Event::BUFFER|Event::WRITE, 0, 0,

        MARK, 5000,
        RESUME,
        TRANSMITER, 5, 5,
        PAUSE,

        MARK, 6000,
        RESUME,
        PAUSE,

        MARK, 7000,
        RESUME,
        PAUSE,
        HANDLER, Event::ERROR, 6, 0,

        MARK, 8000,
        RESUME,
        TRANSMITER, 6, 4,
        HANDLER, Event::BUFFER|Event::WRITE, 2, 0,
        TRANSMITER, 8, 4,
        HANDLER, Event::BUFFER|Event::WRITE, 4, 0,

        MARK, 9000,
        TRANSMITER_ERR, 4, 13,
        PAUSE,
        HANDLER, Event::BUFFER|Event::ERROR, 4, 13,

        MARK, 1000,
        RESUME,
        TRANSMITER, 4, 0,
        PAUSE,
        HANDLER, Event::BUFFER|Event::ERROR, 4, 0,



                          // clang-format on
                      }));
}
