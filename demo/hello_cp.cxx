#include <iostream>
#include <squall/PlatformLoop.hxx>
#include <squall/Dispatcher.hxx>


using squall::Event;
using squall::PlatformLoop;
using Dispatcher = squall::Dispatcher<const char*>;


int main(int argc, char const* argv[]) {
    auto sp_loop = PlatformLoop::create();

    Dispatcher disp([&](const char* name, int revents) {
        if (revents == Event::Timeout) {
            std::cout << "Hello, " << name << "! (" << revents << ")" << std::endl;
        } else if (revents == Event::Cleanup) {
            if (strcmp("SIGINT", name) != 0)
                std::cout << "Bye, " << name << "! (" << revents << ")" << std::endl;
        } else if (revents == Event::Signal) {
            std::cout << "\nGot " << name << ". (" << revents << ")" << std::endl;
            sp_loop->stop();
        }
    }, sp_loop);

    disp.setup_timer("Alesh", 1.0);
    disp.setup_timer("World", 2.5);
    disp.setup_signal("SIGINT", SIGINT);
    sp_loop->start();
    return 0;
}