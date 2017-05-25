#include <string>
#include <memory>
#include <squall/Dispatcher.hxx>
#include <squall/PlatformLoop.hxx>
#include "catch.hpp"

using squall::Event;
using squall::PlatformLoop;
using squall::Dispatcher;


TEST_CASE("Contexted event dispatcher; unit test", "[squall::Dispatcher]") {

    int cnt = 0;
    std::string result;
    const char* A = "A";
    const char* B = "B";
    const char* R = "R";
    const char* W = "W";

    auto sp_loop = PlatformLoop::createShared();
    REQUIRE(sp_loop.use_count() == 1);

    Dispatcher<char const*> disp(
        [&](const char* ch, int revents, void* payload) {

            result += (revents != Event::CLEANUP) ? ch : "C";
            if (ch[0] == 'A') {
                cnt++;
                if (cnt == 1) {
                    disp.setupIoWatching(R, 0, Event::READ);
                    disp.setupIoWatching(W, 0, Event::WRITE);
                } else if (cnt == 6)
                    disp.cancelTimerWatching(B);
                else if (cnt == 10)
                    sp_loop->stop();
            } else if (ch[0] == 'W')
                disp.cancelIoWatching(W);
        },
        sp_loop);

    REQUIRE(sp_loop.use_count() == 2);
    disp.setupTimerWatching(A, 0.1);
    disp.setupTimerWatching(B, 0.26);
    sp_loop->start();

    REQUIRE(result == "AWABAAABAAAAA");

    disp.release();
    REQUIRE(sp_loop.use_count() == 1);
    REQUIRE(result == "AWABAAABAAAAACC");
};
