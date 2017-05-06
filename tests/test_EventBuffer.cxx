#include <string>
#include <memory>
#include <squall/Dispatcher.hxx>
#include <squall/EventBuffer.hxx>
#include <squall/PlatformLoop.hxx>
#include "catch.hpp"

using squall::Event;
using squall::EventBuffer;
using squall::PlatformLoop;
using squall::Dispatcher;


std::pair<int, int> make_fd() {
    int r = 0, w = 0;
    std::string tempname = "squallXXXXXX";
    char* tmp = mkdtemp(const_cast<char*>(tempname.c_str()));
    if (tmp != nullptr) {
        tempname = tmp;
        tempname += "/fifo";
        if (mkfifo(tempname.c_str(), S_IWUSR | S_IRUSR) == 0) {
            r = open(const_cast<char*>(tempname.c_str()), O_RDONLY | O_NONBLOCK);
            w = open(const_cast<char*>(tempname.c_str()), O_WRONLY | O_NONBLOCK);
        }
    }
    return std::make_pair(r, w);
}


inline std::vector<char> cnv(const std::string& str) {
    return std::vector<char>(str.begin(), str.end());
}

inline std::string cnv(const std::vector<char>& vec) {
    return std::string(vec.begin(), vec.end());
}


TEST_CASE("Contexted event dispatcher; EventBuffer", "[squall::EventBuffer]") {

    int tick = 0;
    std::vector<int> ctxs;

    auto files = make_fd();
    REQUIRE(files.first > 0);
    REQUIRE(files.second > 0);

    auto sp_loop = PlatformLoop::create();
    enum Ctx : int { Tick = 0, Writer = 20, Reader = 40, Finish = 90, Cleanup = 100 };

    EventBuffer *writer, *reader;
    std::function<void(int, int)> writer_handler, reader_handler;

    Dispatcher<int> disp(
        [&](int ctx, int revents) {
            if (revents == Event::Cleanup)
                ctx = Ctx::Cleanup;

            int result;
            std::vector<char> v;
            switch (ctx) {

            case Ctx::Tick:
                tick++;
                ctxs.push_back(-tick);
                switch (tick) {


                case 1:
                    // write first block of data
                    REQUIRE(writer->outcomingSize() == 0);
                    REQUIRE(writer->write(cnv("AAA\nBBB")) == 7);
                    REQUIRE(writer->outcomingSize() == 7);
                    REQUIRE(writer->setup_transmiter() == -1);
                    REQUIRE(writer->pendingEvent() == Event::Write);
                    break;

                case 2: // try read data
                    // Try to read first block
                    result = reader->setup_receiver(cnv("\n"));
                    REQUIRE(result == Event::Read); // can read data already has in buffer
                    REQUIRE(cnv(reader->read()) == "AAA\n");
                    result = reader->setup_receiver(cnv("\r\n"));
                    REQUIRE(result == -1); // should wait data
                    break;

                case 3:
                    // write second block of data
                    REQUIRE(writer->outcomingSize() == 0);
                    REQUIRE(writer->write(cnv("\r\n0123456789")) == 12);
                    REQUIRE(writer->outcomingSize() == 12);
                    break;

                case 4:
                    // write third block of data
                    REQUIRE(writer->outcomingSize() == 0);
                    REQUIRE(writer->write(cnv("ABCDEFXXX")) == 9);
                    REQUIRE(writer->outcomingSize() == 9);
                    break;

                case 5:
                    // write big block of data

                    v.resize(16 * 1024, 'A');
                    REQUIRE(writer->outcomingSize() == 0);
                    REQUIRE(writer->write(v) == 16384);
                    REQUIRE(writer->outcomingSize() == 16384);
                    break;

                case 6:
                    v.resize(12 * 1024, 'A');
                    REQUIRE(writer->outcomingSize() == 0);
                    for (auto i = 0; i < 16; i++) {
                        if (i < 6)
                            REQUIRE(writer->write(v) == 12 * 1024);
                        else if (i == 6)
                            REQUIRE(writer->write(v) == 8 * 1024);
                        else
                            REQUIRE(writer->write(v) == 0);
                    }
                    REQUIRE(writer->outcomingSize() == writer->bufferSize());
                    REQUIRE(writer->mode() == 3);
                    break;
                }
                break;


            case Ctx::Writer:
                ctxs.push_back(ctx + tick);
                writer_handler(tick, revents);
                break;

            case Ctx::Reader:
                ctxs.push_back(ctx + tick);
                reader_handler(tick, revents);
                break;

            case Ctx::Finish:
                ctxs.push_back(ctx + tick);
                REQUIRE(reader->incomingSize() == reader->bufferSize());
                REQUIRE(reader->mode() == 0);
                sp_loop->stop();
                break;
            }
        },
        sp_loop);

    reader = disp.create_buffer(Ctx::Reader, files.first);
    writer = disp.create_buffer(Ctx::Writer, files.second, 16000, 90000);
    REQUIRE(reader != nullptr);
    REQUIRE(writer != nullptr);

    SECTION("Checking how has been adjust buffer and block sizes") {
        REQUIRE(reader->blockSize() == 1024);
        REQUIRE(reader->bufferSize() == 1024 * 4);
        REQUIRE(writer->blockSize() == 1024 * 16);
        REQUIRE(writer->bufferSize() == 1024 * 16 * 5);
    }

    disp.setup_timer(Ctx::Tick, 0.03);
    disp.setup_timer(Ctx::Finish, 0.21);
    writer->setup_transmiter();

    writer_handler = [&](int tick, int revents) {
        REQUIRE(revents == Event::Write);
        REQUIRE(writer->pendingEvent() == Event::Write); ////
        REQUIRE(((tick == 0) || (tick == 1)));
        switch (tick) {
        default:
            REQUIRE(writer->cancel());
            REQUIRE(writer->pendingEvent() == 0);
        }
    };

    reader_handler = [&](int tick, int revents) {
        int result;
        REQUIRE((revents & Event::Read) == Event::Read);
        REQUIRE(reader->pendingEvent() == Event::Read); ////
        REQUIRE(((tick == 3) || (tick == 4) || (tick == 5)));
        switch (tick) {
        case 3:
            REQUIRE(reader->mode() == 1);
            REQUIRE(revents == Event::Read);
            // can read second block of data
            REQUIRE(reader->incomingSize() == 15);
            REQUIRE(cnv(reader->read()) == "BBB\r\n");
            REQUIRE(reader->cancel() == false); // read cancel it
            REQUIRE(reader->pendingEvent() == 0);
            REQUIRE(reader->incomingSize() == 10);
            REQUIRE(cnv(reader->read()) == "");

            // setup read until delimiter with max_size
            result = reader->setup_receiver(cnv("\t\t"), 10);
            REQUIRE(result == (Event::Read | Event::Error));
            // delimiter not found but max size exceeded
            REQUIRE(cnv(reader->read()) == ""); // nothing to read

            // setup read exactly number of bytes
            result = reader->setup_receiver(8);
            REQUIRE(reader->incomingSize() == 10);
            // can read early data
            REQUIRE(result == Event::Read);
            REQUIRE(cnv(reader->read()) == "01234567");
            // and one more
            result = reader->setup_receiver(8);
            REQUIRE(result == -1); // should wait data
            REQUIRE(reader->incomingSize() == 2);
            break;

        case 4:
            REQUIRE(reader->mode() == 1);
            REQUIRE(revents == Event::Read);
            REQUIRE(reader->incomingSize() == 11);
            // can read block of data requested in above
            REQUIRE(cnv(reader->read()) == "89ABCDEF");
            // just read buffer how much is there, but no more than 100 bytes
            REQUIRE(cnv(reader->read(100)) == "XXX");

            // setup read until delimiter that would not be received
            result = reader->setup_receiver(cnv("ZZZ"));
            REQUIRE(result == -1); // should wait data
            break;

        case 5:
            REQUIRE(reader->mode() == 0);
            REQUIRE(reader->incomingSize() >= reader->bufferSize());
            // delimiter not found but buffer full
            REQUIRE(revents == (Event::Read | Event::Error));

            // read 100 bytes
            REQUIRE(cnv(reader->read(100)).size() == 100);
            REQUIRE(reader->mode() == 1);
            break;

        default:
            REQUIRE(reader->cancel());
            REQUIRE(reader->pendingEvent() == 0);
        }
    };

    sp_loop->start();
    disp.release_buffer(writer);

    REQUIRE(ctxs == std::vector<int>({-1, 21, -2, -3, 43, -4, 44, -5, 45, -6, -7, 97}));
};
