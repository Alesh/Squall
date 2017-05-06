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
    const char* W = "W";

    auto sp_loop = PlatformLoop::create();
    REQUIRE(sp_loop.use_count() == 1);

    Dispatcher<char const*> disp(
        [&](const char* ch, int revents) {

            result += (revents != Event::Cleanup) ? ch : "C";
            if (ch[0] == 'A') {
                cnt++;
                if (cnt == 1)
                    disp.setup_io(W, 0, Event::Write);
                else if (cnt == 6)
                    disp.cancel_timer(B);
                else if (cnt == 10)
                    sp_loop->stop();
            } else if (ch[0] == 'W')
                disp.cancel_io(W);
        },
        sp_loop);

    REQUIRE(sp_loop.use_count() == 2);
    disp.setup_timer(A, 0.1);
    disp.setup_timer(B, 0.26);
    sp_loop->start();

    REQUIRE(result == "AWABAAABAAAAA");

    disp.cleanup();
    REQUIRE(sp_loop.use_count() == 1);
    REQUIRE(result == "AWABAAABAAAAAC");
};
