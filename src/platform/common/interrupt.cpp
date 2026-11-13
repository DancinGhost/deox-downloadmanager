// SPDX-License-Identifier: MIT
//
// Operating-system independent part of deoxdm/platform/interrupt.hpp. The
// handlers themselves live in the backends.

#include "deoxdm/platform/interrupt.hpp"
#include "backend.hpp"

namespace deoxdm::platform {

namespace detail {
std::atomic<bool> g_interrupt_requested{false};
} // namespace detail

bool interrupt_requested() noexcept {
    return detail::g_interrupt_requested.load(std::memory_order_relaxed);
}

} // namespace deoxdm::platform
