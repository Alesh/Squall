#include <iostream>
#include "EventLoop.hxx"

using squall::Event;
using squall::EventLoop;


int main(int argc, char const* argv[]) {

    int cnt = 0;
    EventLoop event_loop;

    auto handle = event_loop.setupTimerWatching([](int revents) {
        if (revents == Event::TIMEOUT)
            std::cout << "Hello, Alesh! (" << revents << ")" << std::endl;
        if (revents == Event::CLEANUP)
            std::cout << "Bye, Alesh! (" << revents << ")" << std::endl;
    }, 1.0);

    event_loop.setupTimerWatching([&](int revents) {
        if (revents == Event::TIMEOUT) {
            std::cout << "Hello, World! (" << revents << ")" << std::endl;
            cnt++;
        }
        if (revents == Event::CLEANUP)
            std::cout << "Bye, World! (" << revents << ")" << std::endl;
        if (cnt > 3)
            event_loop.cancelTimerWatching(handle);
    }, 2.5);

    event_loop.setupTimeoutWatching([&](int revents) {
        std::cout << "The show has done!" << std::endl;
        event_loop.stop();
    }, 60);

    event_loop.setupSignalWatching([&event_loop](int revents) {
        if (revents == Event::SIGNAL) {
            std::cout << "\nGot SIGINT. (" << revents << ")" << std::endl;
            event_loop.stop();
        }
    }, SIGINT);

    event_loop.start();
    return 0;
}