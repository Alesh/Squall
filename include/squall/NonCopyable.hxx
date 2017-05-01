#ifndef SQUALL__NON_COPYABLE_HXX
#define SQUALL__NON_COPYABLE_HXX

namespace squall {

/* Instance of this class be no copyable. */
class NonCopyable {
  protected:
    NonCopyable() {}

  private:
    NonCopyable(const NonCopyable&) = delete;
    void operator=(const NonCopyable&) = delete;
};
}
#endif // SQUALL__NON_COPYABLE_HXX