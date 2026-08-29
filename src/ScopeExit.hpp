#pragma once

#include <utility>

namespace Xenu
{
template<typename Callback>
class ScopeExit
{
public:
    explicit ScopeExit(Callback callback)
    : m_callback(std::move(callback))
    {
    }

    ~ScopeExit()
    {
        m_callback();
    }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    Callback m_callback;
};
}
