#pragma once

// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2016-present Gabi Melman and spdlog contributors
// SPDX-FileCopyrightText: 2026 Ariyali_
//
// Portions derived from spdlog.
// Distributed under the MIT License; see THIRD_PARTY_NOTICES.md.

#include "./namespace.h"
#include "./types.h"
#include "./macros.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX  // prevent windows redefining min/max
#  endif

#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif

#  include <windows.h>
#  include <io.h>       // for _get_osfhandle, _isatty, _fileno
#  include <process.h>  // for _get_pid

#  ifdef __MINGW32__
#    include <share.h>
#  endif

#  include <cassert>
#  include <limits>
#  include <direct.h>  // for _mkdir/_wmkdir

#else  // unix

#  include <fcntl.h>
#  include <unistd.h>

#  ifdef __linux__
#    include <sys/syscall.h>  //Use gettid() syscall under linux to get thread id

#  elif defined(_AIX)
#    include <pthread.h>  // for pthread_getthrds_np

#  elif defined(__DragonFly__) || defined(__FreeBSD__)
#    include <pthread_np.h>  // for pthread_getthreadid_np

#  elif defined(__NetBSD__)
#    include <lwp.h>  // for _lwp_self

#  elif defined(__sun)
#    include <thread.h>  // for thr_self
#  endif

#endif  // unix

#if defined __APPLE__
#  include <AvailabilityMacros.h>
#endif

NOVA_NAMESPACE_BEGIN
namespace core {

// eol definition
#if !defined(NOVA_EOL)
#  ifdef _WIN32
#    define NOVA_EOL "\r\n"
#  else
#    define NOVA_EOL "\n"
#  endif
#endif

NOVA_CONSTEXPR static const char *kDefaultEol = NOVA_EOL;

// folder separator
#if !defined(NOVA_FOLDER_SEPS)
#  ifdef _WIN32
#    define NOVA_FOLDER_SEPS "\\/"
#  else
#    define NOVA_FOLDER_SEPS "/"
#  endif
#endif

NOVA_CONSTEXPR static const char kFolderSeps[] = NOVA_FOLDER_SEPS;
NOVA_CONSTEXPR static const Filename::value_type kFolderSepsFilename[] = NOVA_FILENAME(NOVA_FOLDER_SEPS);

NOVA_INLINE std::string TrimedLeft(const std::string &s)
{
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        ++i;
    return s.substr(i);
}

NOVA_INLINE std::string TrimedRight(const std::string &s)
{
    size_t i = s.size();
    while (i > 0 && std::isspace(static_cast<unsigned char>(s[i - 1])))
        --i;
    return s.substr(0, i);
}

NOVA_INLINE std::string Trimed(const std::string &s)
{
    return TrimedRight(TrimedLeft(s));
}

NOVA_INLINE SteadyTimePoint SteadyNowTime()
{
    return std::chrono::steady_clock::now();
}

NOVA_INLINE Int64 ElapsedMs(SteadyTimePoint since)
{
    return std::chrono::duration_cast<Milliseconds>(std::chrono::steady_clock::now() - since).count();
}

NOVA_INLINE SystemClock::time_point Now() NOVA_NOEXCEPT
{
#if defined __linux__ && defined NOVA_CLOCK_COARSE
    timespec ts;
    ::clock_gettime(CLOCK_REALTIME_COARSE, &ts);
    return std::chrono::time_point<SystemClock, typename SystemClock::duration>(
        std::chrono::duration_cast<typename SystemClock::duration>(std::chrono::seconds(ts.tv_sec) +
                                                                   std::chrono::nanoseconds(ts.tv_nsec)));

#else
    return SystemClock::now();
#endif
}
NOVA_INLINE std::tm Localtime(const std::time_t &time_tt) NOVA_NOEXCEPT
{
#ifdef _WIN32
    std::tm tm;
    ::localtime_s(&tm, &time_tt);
#else
    std::tm tm;
    ::localtime_r(&time_tt, &tm);
#endif
    return tm;
}

NOVA_INLINE std::tm Localtime() NOVA_NOEXCEPT
{
    std::time_t now = ::time(nullptr);
    return Localtime(now);
}

NOVA_INLINE std::tm Gmtime(const std::time_t &time_tt) NOVA_NOEXCEPT
{
#ifdef _WIN32
    std::tm tm;
    ::gmtime_s(&tm, &time_tt);
#else
    std::tm tm;
    ::gmtime_r(&time_tt, &tm);
#endif
    return tm;
}

NOVA_INLINE std::tm Gmtime() NOVA_NOEXCEPT
{
    std::time_t now_t = ::time(nullptr);
    return Gmtime(now_t);
}

// Fopen_s on non windows for writing
NOVA_INLINE bool Fopen_s(FILE **fp, const Filename &filename, const Filename &mode)
{
#ifdef _WIN32
#  ifdef NOVA_WCHAR_FILENAMES
    *fp = ::_wfsopen((filename.c_str()), mode.c_str(), _SH_DENYNO);
#  else
    *fp = ::_fsopen((filename.c_str()), mode.c_str(), _SH_DENYNO);
#  endif
#  if defined(NOVA_PREVENT_CHILD_FD)
    if (*fp != nullptr) {
        auto file_handle = reinterpret_cast<HANDLE>(_get_osfhandle(::_fileno(*fp)));
        if (!::SetHandleInformation(file_handle, HANDLE_FLAG_INHERIT, 0)) {
            ::fclose(*fp);
            *fp = nullptr;
        }
    }
#  endif
#else  // unix
#  if defined(NOVA_PREVENT_CHILD_FD)
    const int mode_flag = mode == NOVA_FILENAME("ab") ? O_APPEND : O_TRUNC;
    const int fd = ::open((filename.c_str()), O_CREAT | O_WRONLY | O_CLOEXEC | mode_flag, mode_t(0644));
    if (fd == -1) {
        return true;
    }
    *fp = ::fdopen(fd, mode.c_str());
    if (*fp == nullptr) {
        ::close(fd);
    }
#  else
    *fp = ::fopen((filename.c_str()), mode.c_str());
#  endif
#endif

    return *fp == nullptr;
}

NOVA_INLINE int Remove(const Filename &filename) NOVA_NOEXCEPT
{
#if defined(_WIN32) && defined(NOVA_WCHAR_FILENAMES)
    return ::_wremove(filename.c_str());
#else
    return std::remove(filename.c_str());
#endif
}

NOVA_INLINE int RemoveIfExists(const Filename &filename) NOVA_NOEXCEPT
{
    if (Remove(filename) == 0)
        return 0;

    return (errno == ENOENT) ? 0 : -1;
}

NOVA_INLINE int Rename(const Filename &filename1, const Filename &filename2) NOVA_NOEXCEPT
{
#if defined(_WIN32) && defined(NOVA_WCHAR_FILENAMES)
    return ::_wrename(filename1.c_str(), filename2.c_str());
#else
    return std::rename(filename1.c_str(), filename2.c_str());
#endif
}

// Return true if path exists (file or directory)
NOVA_INLINE bool PathExists(const Filename &filename) NOVA_NOEXCEPT
{
#ifdef _WIN32
    struct _stat buffer;
#  ifdef NOVA_WCHAR_FILENAMES
    return (::_wstat(filename.c_str(), &buffer) == 0);
#  else
    return (::_stat(filename.c_str(), &buffer) == 0);
#  endif
#else  // common linux/unix all have the stat system call
    struct stat buffer;
    return (::stat(filename.c_str(), &buffer) == 0);
#endif
}

#ifdef _MSC_VER
// avoid warning about unreachable statement at the end of Filesize()
#  pragma warning(push)
#  pragma warning(disable : 4702)
#endif

// Return file size according to open FILE* object
NOVA_INLINE size_t Filesize(FILE *f)
{
    if (f == nullptr) {
        NOVA_THROW(std::runtime_error("Failed getting file size. fd is null"));
    }
#if defined(_WIN32) && !defined(__CYGWIN__)
    int fd = ::_fileno(f);
#  if defined(_WIN64)  // 64 bits
    __int64 ret = ::_filelengthi64(fd);
    if (ret >= 0) {
        return static_cast<size_t>(ret);
    }

#  else  // windows 32 bits
    long ret = ::_filelength(fd);
    if (ret >= 0) {
        return static_cast<size_t>(ret);
    }
#  endif

#else  // unix
// OpenBSD and AIX doesn't compile with :: before the fileno(..)
#  if defined(__OpenBSD__) || defined(_AIX)
    int fd = fileno(f);
#  else
    int fd = ::fileno(f);
#  endif
// 64 bits(but not in osx, linux/musl or cygwin, where fstat64 is deprecated)
#  if ((defined(__linux__) && defined(__GLIBC__)) || defined(__sun) || defined(_AIX)) &&                     \
      (defined(__LP64__) || defined(_LP64))
    struct stat64 st;
    if (::fstat64(fd, &st) == 0) {
        return static_cast<size_t>(st.st_size);
    }
#  else  // other unix or linux 32 bits or cygwin
    struct stat st;
    if (::fstat(fd, &st) == 0) {
        return static_cast<size_t>(st.st_size);
    }
#  endif
#endif
    NOVA_THROW(
        std::runtime_error(std::string("Failed getting file size from fd, errno ") + std::to_string(errno)));
    return 0;  // will not be reached.
}

#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#if !defined(NOVA_NO_TZ_OFFSET)
#  ifdef _WIN32
// Compare the timestamp as Local (mktime) vs UTC (_mkgmtime) to get the offset.
NOVA_INLINE int UtcMinutesOffset(const std::tm &tm)
{
    std::tm local_tm = tm;  // copy since mktime might adjust it (normalize dates, set tm_isdst)
    std::time_t local_time_t = std::mktime(&local_tm);
    if (local_time_t == -1) {
        return 0;  // fallback
    }

    std::time_t utc_time_t = _mkgmtime(&local_tm);
    if (utc_time_t == -1) {
        return 0;  // fallback
    }
    auto offset_seconds = utc_time_t - local_time_t;
    return static_cast<int>(offset_seconds / 60);
}
#  else
// On unix simply use tm_gmtoff
NOVA_INLINE int UtcMinutesOffset(const std::tm &tm)
{
    return static_cast<int>(tm.tm_gmtoff / 60);
}
#  endif  // _WIN32
#endif    // NOVA_NO_TZ_OFFSET

// Return current thread id as size_t
// It exists because the std::this_thread::get_id() is much slower(especially
// under VS 2013)
NOVA_INLINE size_t _ThreadId() NOVA_NOEXCEPT
{
#ifdef _WIN32
    return static_cast<size_t>(::GetCurrentThreadId());
#elif defined(__linux__)
#  if defined(__ANDROID__) && defined(__ANDROID_API__) && (__ANDROID_API__ < 21)
#    define SYS_gettid __NR_gettid
#  endif
    return static_cast<size_t>(::syscall(SYS_gettid));
#elif defined(_AIX)
    struct __pthrdsinfo buf;
    int reg_size = 0;
    pthread_t pt = pthread_self();
    int retval = pthread_getthrds_np(&pt, PTHRDSINFO_QUERY_TID, &buf, sizeof(buf), NULL, &reg_size);
    int tid = (!retval) ? buf.__pi_tid : 0;
    return static_cast<size_t>(tid);
#elif defined(__DragonFly__) || defined(__FreeBSD__)
    return static_cast<size_t>(::pthread_getthreadid_np());
#elif defined(__NetBSD__)
    return static_cast<size_t>(::_lwp_self());
#elif defined(__OpenBSD__)
    return static_cast<size_t>(::getthrid());
#elif defined(__sun)
    return static_cast<size_t>(::thr_self());
#elif __APPLE__
    Uint64 tid;
// There is no pthread_threadid_np prior to Mac OS X 10.6, and it is not supported on any PPC,
// including 10.6.8 Rosetta. __POWERPC__ is Apple-specific define encompassing ppc and ppc64.
#  ifdef MAC_OS_X_VERSION_MAX_ALLOWED
    {
#    if (MAC_OS_X_VERSION_MAX_ALLOWED < 1060) || defined(__POWERPC__)
        tid = pthread_mach_thread_np(pthread_self());
#    elif MAC_OS_X_VERSION_MIN_REQUIRED < 1060
        if (&pthread_threadid_np) {
            pthread_threadid_np(nullptr, &tid);
        } else {
            tid = pthread_mach_thread_np(pthread_self());
        }
#    else
        pthread_threadid_np(nullptr, &tid);
#    endif
    }
#  else
    pthread_threadid_np(nullptr, &tid);
#  endif
    return static_cast<size_t>(tid);
#else  // Default to standard C++11 (other Unix)
    return static_cast<size_t>(std::hash<std::thread::id>()(std::this_thread::get_id()));
#endif
}

// Return current thread id as size_t (from thread local storage)
NOVA_INLINE size_t ThreadId() NOVA_NOEXCEPT
{
#if defined(NOVA_NO_TLS)
    return _ThreadId();
#else  // cache thread id in tls
    static thread_local const size_t tid = _ThreadId();
    return tid;
#endif
}

// This is avoid msvc issue in sleep_for that happens if the clock changes.
// See https://github.com/gabime/spdlog/issues/609
NOVA_INLINE void SleepForMillis(unsigned int milliseconds) NOVA_NOEXCEPT
{
#if defined(_WIN32)
    ::Sleep(milliseconds);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}

// wchar support for windows file names (NOVA_WCHAR_FILENAMES must be defined)
#if defined(_WIN32) && defined(NOVA_WCHAR_FILENAMES)
NOVA_INLINE std::string FilenameToStr(const Filename &filename)
{
    if (filename.empty()) {
        return std::string();
    }

    int size_needed =
        WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), (int)filename.size(), NULL, 0, NULL, NULL);
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), (int)filename.size(), &result[0], size_needed, NULL,
                        NULL);

    return result;
}
#else
NOVA_INLINE std::string FilenameToStr(const Filename &filename)
{
    return filename;
}
#endif

NOVA_INLINE int Pid() NOVA_NOEXCEPT
{
#ifdef _WIN32
    return conditional_static_cast<int>(::GetCurrentProcessId());
#else
    return conditional_static_cast<int>(::getpid());
#endif
}

NOVA_INLINE bool IsColorTerminal() NOVA_NOEXCEPT
{
#ifdef _WIN32
    return true;
#else

    static const bool result = []() {
        const char *env_colorterm_p = std::getenv("COLORTERM");
        if (env_colorterm_p != nullptr) {
            return true;
        }

        static constexpr std::array<const char *, 16> terms = {
            {"ansi", "color", "console", "cygwin", "gnome", "konsole", "kterm", "linux", "msys", "putty",
             "rxvt", "screen", "vt100", "xterm", "alacritty", "vt102"}
        };

        const char *env_term_p = std::getenv("TERM");
        if (env_term_p == nullptr) {
            return false;
        }

        return std::any_of(terms.begin(), terms.end(),
                           [&](const char *term) { return std::strstr(env_term_p, term) != nullptr; });
    }();

    return result;
#endif
}

// Determine if the terminal attached
// Source: https://github.com/agauniyal/rang/
NOVA_INLINE bool InTerminal(FILE *file) NOVA_NOEXCEPT
{
#ifdef _WIN32
    return ::_isatty(_fileno(file)) != 0;
#else
    return ::isatty(fileno(file)) != 0;
#endif
}

#if (defined(NOVA_WCHAR_TO_UTF8_SUPPORT) || defined(NOVA_WCHAR_FILENAMES)) && defined(_WIN32)
NOVA_INLINE void WstrToUtf8buf(wstring_view_t wstr, std::string &target)
{
    if (wstr.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) / 4 - 1) {
        NOVA_THROW(std::runtime_error("UTF-16 string is too big to be converted to UTF-8"));
    }

    int wstr_size = static_cast<int>(wstr.size());
    if (wstr_size == 0) {
        target.clear();
        return;
    }

    int result_size = ::WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr_size, nullptr, 0, nullptr, nullptr);

    if (result_size > 0) {
        target.resize(result_size);

        result_size = ::WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wstr_size, target.data(), result_size,
                                            nullptr, nullptr);

        if (result_size > 0) {
            target.resize(result_size);
            return;
        }
    }

    NOVA_THROW(std::runtime_error(std::string("WideCharToMultiByte failed. Last error: ") +
                                  std::to_string(::GetLastError())));
}

NOVA_INLINE void Utf8ToWstrbuf(string_view_t str, std::wstring &target)
{
    if (str.size() > static_cast<size_t>((std::numeric_limits<int>::max)()) - 1) {
        NOVA_THROW(std::runtime_error("UTF-8 string is too big to be converted to UTF-16"));
    }

    int str_size = static_cast<int>(str.size());
    if (str_size == 0) {
        target.clear();
        return;
    }

    int result_size = ::MultiByteToWideChar(CP_UTF8, 0, str.data(), str_size, nullptr, 0);

    if (result_size > 0) {
        target.resize(result_size);  // 扩展 wstring 空间

        result_size = ::MultiByteToWideChar(CP_UTF8, 0, str.data(), str_size, target.data(), result_size);
        if (result_size > 0) {
            assert(result_size == static_cast<int>(target.size()));
            return;
        }
    }

    NOVA_THROW(std::runtime_error(std::string("MultiByteToWideChar failed. Last error: ") +
                                  std::to_string(::GetLastError())));
}
#endif  // (defined(NOVA_WCHAR_TO_UTF8_SUPPORT) || defined(NOVA_WCHAR_FILENAMES)) &&
        // defined(_WIN32)

// return true on success
static NOVA_INLINE bool Mkdir(const Filename &path)
{
#ifdef _WIN32
#  ifdef NOVA_WCHAR_FILENAMES
    return ::_wmkdir(path.c_str()) == 0;
#  else
    return ::_mkdir(path.c_str()) == 0;
#  endif
#else
    return ::mkdir(path.c_str(), mode_t(0755)) == 0;
#endif
}

// create the given directory - and all directories leading to it
// return true on success or if the directory already exists
NOVA_INLINE bool CreateDir(const Filename &path)
{
    if (PathExists(path)) {
        return true;
    }

    if (path.empty()) {
        return false;
    }

    size_t search_offset = 0;
    do {
        auto token_pos = path.find_first_of(kFolderSepsFilename, search_offset);
        // treat the entire path as a folder if no folder separator not found
        if (token_pos == Filename::npos) {
            token_pos = path.size();
        }

        auto subdir = path.substr(0, token_pos);
#ifdef _WIN32
        // if subdir is just a drive letter, add a slash e.g. "c:"=>"c:\",
        // otherwise path_exists(subdir) returns false (issue #3079)
        const bool is_drive = subdir.length() == 2 && subdir[1] == ':';
        if (is_drive) {
            subdir += '\\';
            token_pos++;
        }
#endif

        if (!subdir.empty() && !PathExists(subdir) && !Mkdir(subdir)) {
            if (errno != EEXIST) {
                return false;  // return error if failed creating dir
            }
        }
        search_offset = token_pos + 1;
    } while (search_offset < path.size());

    return true;
}

// Return directory name from given path or empty string
// "abc/file" => "abc"
// "abc/" => "abc"
// "abc" => ""
// "abc///" => "abc//"
NOVA_INLINE Filename DirName(const Filename &path)
{
    auto pos = path.find_last_of(kFolderSepsFilename);
    return pos != Filename::npos ? path.substr(0, pos) : Filename{};
}

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4996)
#endif  // _MSC_VER
std::string NOVA_INLINE Getenv(const char *field)
{
#if defined(_MSC_VER) && defined(WINAPI_FAMILY) && defined(WINAPI_FAMILY_DESKTOP_APP) &&                     \
    (WINAPI_FAMILY != WINAPI_FAMILY_DESKTOP_APP)
    return std::string{};  // not supported under uwp
#else
    char *buf = std::getenv(field);
    return buf ? buf : std::string{};
#endif
}
#ifdef _MSC_VER
#  pragma warning(pop)
#endif  // _MSC_VER

// Do fsync by FILE handlerpointer
// Return true on success
NOVA_INLINE bool Fsync(FILE *fp)
{
#ifdef _WIN32
    return FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(fp)))) != 0;
#else
    return ::fsync(fileno(fp)) == 0;
#endif
}

// Do non-locking fwrite if possible by the os or use the regular locking fwrite
// Return true on success.
NOVA_INLINE bool FwriteBytes(const void *ptr, const size_t n_bytes, FILE *fp)
{
#if defined(_WIN32) && defined(NOVA_FWRITE_UNLOCKED)
    return _fwrite_nolock(ptr, 1, n_bytes, fp) == n_bytes;
#elif defined(NOVA_FWRITE_UNLOCKED)
    return ::fwrite_unlocked(ptr, 1, n_bytes, fp) == n_bytes;
#else
    return std::fwrite(ptr, 1, n_bytes, fp) == n_bytes;
#endif
}

}  // namespace nova:core
NOVA_NAMESPACE_END  // namespace nova
