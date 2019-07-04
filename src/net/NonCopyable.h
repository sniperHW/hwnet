#ifndef _NON_COPYABLE
#define _NON_COPYABLE

namespace hwnet {

class NonCopyable
{
public:
    NonCopyable(const NonCopyable&) = delete;
    const NonCopyable& operator=(const NonCopyable&) = delete;

protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
};

}


#endif