#ifndef SQUALL__CORE__EXCEPTIONS_HXX
#define SQUALL__CORE__EXCEPTIONS_HXX
#include <string>
#include <stdexcept>

namespace squall {
namespace exc {

class CannotSetupWatching : public std::runtime_error {
  public:
    CannotSetupWatching(std::string message = "")
        : std::runtime_error(message.size() > 0 ? message : "Error while set up event watching") {}
};
} // squall::exc
} // squall
#endif // SQUALL__CORE__EXCEPTIONS_HXX