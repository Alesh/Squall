#include <iostream>
#include <squall/PlatformLoop.hxx>
#include <squall/Dispatcher.hxx>


using squall::Event;
using squall::PlatformLoop;
using Dispatcher = squall::Dispatcher<const char*>;


int main(int argc, char const* argv[]) {
    auto sp_loop = PlatformLoop::createShared();

    Dispatcher disp([&](const char* name, int revents, void* payload) {
        if (revents == Event::TIMEOUT) {
            std::cout << "Hello, " << name << "! (" << revents << ")" << std::endl;
        } else if (revents == Event::CLEANUP) {
            if (strcmp("SIGINT", name) != 0)
                std::cout << "Bye, " << name << "! (" << revents << ")" << std::endl;
        } else if (revents == Event::SIGNAL) {
            std::cout << "\nGot " << name << ". (" << revents << ")" << std::endl;
            sp_loop->stop();
        }
    }, sp_loop);

    disp.setupTimerWatching("Alesh", 1.0);
    disp.setupTimerWatching("World", 2.5);
    disp.setupSignalWatching("SIGINT", SIGINT);
    sp_loop->start();
    return 0;
}