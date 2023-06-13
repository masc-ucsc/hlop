//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// FIXME: In C++20 we do not need this
#ifndef likely
#define likely(x) __builtin_expect((x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect((x), 0)
#endif
