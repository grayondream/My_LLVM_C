#pragma once

#include <functional>
#include <utility>

class ScopeGuard {
public:
    explicit ScopeGuard(std::function<void()> callback) noexcept
        : m_callback(std::move(callback))
        , m_active(true) {}

    ScopeGuard(ScopeGuard&& other) noexcept
        : m_callback(std::move(other.m_callback))
        , m_active(other.m_active) {
        other.m_active = false;
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    ~ScopeGuard() {
        if (m_active && m_callback) {
            m_callback();
        }
    }

    void dismiss() noexcept {
        m_active = false;
    }

    bool isActive() const noexcept {
        return m_active;
    }

private:
    std::function<void()> m_callback;
    bool m_active;
};

namespace detail {
    enum class ScopeGuardOnExit {};

    template<typename F>
    ScopeGuard operator+(ScopeGuardOnExit, F&& f) {
        return ScopeGuard(std::forward<F>(f));
    }
}

#define SCOPE_EXIT \
    auto ANONYMOUS_VARIABLE(scope_guard_) = ::detail::ScopeGuardOnExit{} + [&]()

#define CONCATENATE_IMPL(x, y) x##y
#define CONCATENATE(x, y) CONCATENATE_IMPL(x, y)
#define ANONYMOUS_VARIABLE(name) CONCATENATE(name, __LINE__)
