#ifndef SQUALL__CORE__NON_COPYABLE_HXX
#define SQUALL__CORE__NON_COPYABLE_HXX

namespace squall {
namespace core {

/* Instance of this class be no copyable. */
class NonCopyable {
  protected:
    NonCopyable() {}

  private:
    NonCopyable(const NonCopyable&) = delete;
    void operator=(const NonCopyable&) = delete;
};
} // squall::core
} // squall
#endif // SQUALL__CORE__NON_COPYABLE_HXX