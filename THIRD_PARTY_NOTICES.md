# Third-Party Notices

NovaInfra includes or derives portions of the following third-party software.

## spdlog

The queue and operating-system helper implementations in NovaInfra contain
code derived from spdlog (https://github.com/gabime/spdlog).

Derived source mapping:

| NovaInfra file | Upstream spdlog source |
|---|---|
| `modules/core/include/core/circular_queue.h` | `include/spdlog/details/circular_q.h` |
| `modules/core/include/core/mpmc_blocking_queue.h` | `include/spdlog/details/mpmc_blocking_q.h` |
| `modules/core/include/core/os_utils.h` | `include/spdlog/details/os.h` and `include/spdlog/details/os-inl.h` |

The exact upstream tag or commit used for the original import was not recorded. The mappings above
identify the upstream source files and do not claim an exact source snapshot.

The MIT License (MIT)

Copyright (c) 2016 - present, Gabi Melman and spdlog contributors.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Boost.SML 1.2.0

NovaInfra StateMachine vendors the single-header Boost.SML implementation from
https://github.com/boost-ext/sml (version 1.2.0, commit
`2074a7e826816c140f0b192529f4ac6556f7fad1`, `include/boost/sml.hpp`).

Copyright (c) 2016-2024 Kris Jusiak.

Distributed under the Boost Software License, Version 1.0:
https://www.boost.org/LICENSE_1_0.txt

## fmt 12.2.0

NovaInfra Logger bundles the fmt C++ headers in header-only mode
(https://github.com/fmtlib/fmt).

Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## toml++ 3.4.0

NovaInfra Config uses the single-header toml++ parser at build time
(https://github.com/marzer/tomlplusplus), pinned to version 3.4.0.

Copyright (c) 2021-2024 Mark Gillard.

Distributed under the MIT License:
https://github.com/marzer/tomlplusplus/blob/v3.4.0/LICENSE
