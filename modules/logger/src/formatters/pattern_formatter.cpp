#include <logger/formatters/pattern_formatter.h>

#include <core/os_utils.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::formatters {
namespace {
template <typename Integer>
void AppendInteger(Integer value, std::string &dst)
{
    char buffer[32];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
    if (result.ec == std::errc{}) {
        dst.append(buffer, result.ptr);
    }
}

template <typename Integer>
void AppendIntegerPaddedFallback(Integer value, std::size_t width, std::string &dst)
{
    char buffer[32];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
    if (result.ec != std::errc{}) {
        return;
    }

    const auto length = static_cast<std::size_t>(result.ptr - buffer);
    if (value >= 0 && length < width) {
        dst.append(width - length, '0');
    }
    dst.append(buffer, result.ptr);
}

void WriteTwoDigits(novainfra::Uint32 value, char *dst)
{
    dst[0] = static_cast<char>('0' + value / 10);
    dst[1] = static_cast<char>('0' + value % 10);
}

void WriteThreeDigits(novainfra::Uint32 value, char *dst)
{
    dst[0] = static_cast<char>('0' + value / 100);
    WriteTwoDigits(value % 100, dst + 1);
}

void AppendTwoDigits(novainfra::Uint32 value, std::string &dst)
{
    const auto offset = dst.size();
    dst.resize(offset + 2);
    WriteTwoDigits(value, dst.data() + offset);
}

void AppendThreeDigits(novainfra::Uint32 value, std::string &dst)
{
    const auto offset = dst.size();
    dst.resize(offset + 3);
    WriteThreeDigits(value, dst.data() + offset);
}

template <typename Integer>
void AppendIntegerPadded(Integer value, std::size_t width, std::string &dst)
{
    if constexpr (std::is_signed<Integer>::value) {
        if (value < 0) {
            AppendIntegerPaddedFallback(value, width, dst);
            return;
        }
    }

    const auto unsignedValue = static_cast<novainfra::Uint64>(value);
    switch (width) {
        case 2:
            if (unsignedValue < 100) {
                AppendTwoDigits(static_cast<novainfra::Uint32>(unsignedValue), dst);
                return;
            }
            break;
        case 3:
            if (unsignedValue < 1'000) {
                AppendThreeDigits(static_cast<novainfra::Uint32>(unsignedValue), dst);
                return;
            }
            break;
        case 6:
            if (unsignedValue < 1'000'000) {
                const auto offset = dst.size();
                dst.resize(offset + 6);
                WriteThreeDigits(static_cast<novainfra::Uint32>(unsignedValue / 1'000), dst.data() + offset);
                WriteThreeDigits(static_cast<novainfra::Uint32>(unsignedValue % 1'000),
                                 dst.data() + offset + 3);
                return;
            }
            break;
        case 9:
            if (unsignedValue < 1'000'000'000) {
                const auto offset = dst.size();
                dst.resize(offset + 9);
                WriteThreeDigits(static_cast<novainfra::Uint32>(unsignedValue / 1'000'000),
                                 dst.data() + offset);
                WriteThreeDigits(static_cast<novainfra::Uint32>((unsignedValue / 1'000) % 1'000),
                                 dst.data() + offset + 3);
                WriteThreeDigits(static_cast<novainfra::Uint32>(unsignedValue % 1'000),
                                 dst.data() + offset + 6);
                return;
            }
            break;
        default:
            break;
    }
    AppendIntegerPaddedFallback(value, width, dst);
}

template <typename Duration>
novainfra::Int64 FractionalPart(novainfra::SystemTimePoint timestamp)
{
    const auto sinceEpoch = timestamp.time_since_epoch();
    if (sinceEpoch >= novainfra::SystemClock::duration::zero()) {
        const auto total = std::chrono::duration_cast<Duration>(sinceEpoch).count();
        const auto unitsPerSecond = std::chrono::duration_cast<Duration>(std::chrono::seconds{1}).count();
        return static_cast<novainfra::Int64>(total % unitsPerSecond);
    }
    const auto wholeSeconds = std::chrono::floor<std::chrono::seconds>(sinceEpoch);
    return static_cast<novainfra::Int64>(
        std::chrono::duration_cast<Duration>(sinceEpoch - wholeSeconds).count());
}

std::string_view LevelToString(novainfra::log::Level level)
{
    switch (level) {
        case novainfra::log::Level::TRACE:
            return "TRACE";
        case novainfra::log::Level::DEBUG:
            return "DEBUG";
        case novainfra::log::Level::INFO:
            return "INFO";
        case novainfra::log::Level::WARN:
            return "WARN";
        case novainfra::log::Level::ERROR:
            return "ERROR";
        case novainfra::log::Level::CRITICAL:
            return "CRITICAL";
        default:
            return "";
    }
}

const char *AmPm(const std::tm &time)
{
    return time.tm_hour >= 12 ? "PM" : "AM";
}

int To12Hour(const std::tm &time)
{
    const auto hour = time.tm_hour % 12;
    return hour == 0 ? 12 : hour;
}

std::string_view Basename(std::string_view filename)
{
    const auto position = filename.find_last_of(novainfra::core::kFolderSeps);
    return position == std::string_view::npos ? filename : filename.substr(position + 1);
}

constexpr std::array<std::string_view, 7> kDays{
    {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}
};

constexpr std::array<std::string_view, 7> kFullDays{
    {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}
};

constexpr std::array<std::string_view, 12> kMonths{
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}
};

constexpr std::array<std::string_view, 12> kFullMonths{
    {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
     "November", "December"}
};

constexpr std::string_view kCommonDateTime{"%Y-%m-%d %H:%M:%S"};
constexpr std::string_view kCommonDateTimeMillis{"%Y-%m-%d %H:%M:%S.%e"};
}  // namespace

PatternFormatter::PatternFormatter(PatternTimeType timeType, std::string eol)
    : pattern_("%+"), patternTimeType_(timeType), eol_(std::move(eol))
{
    auto compiled = compilePattern(pattern_);
    program_ = std::move(compiled.program);
    literals_ = std::move(compiled.literals);
    elapsedStates_ = std::move(compiled.elapsedStates);
    fullStates_ = std::move(compiled.fullStates);
    dateTimeStates_ = std::move(compiled.dateTimeStates);
    needLocaltime_ = compiled.needLocaltime;
}

PatternFormatter::PatternFormatter(std::string pattern, PatternTimeType timeType, std::string eol)
    : pattern_(std::move(pattern)), patternTimeType_(timeType), eol_(std::move(eol))
{
    auto compiled = compilePattern(pattern_);
    program_ = std::move(compiled.program);
    literals_ = std::move(compiled.literals);
    elapsedStates_ = std::move(compiled.elapsedStates);
    fullStates_ = std::move(compiled.fullStates);
    dateTimeStates_ = std::move(compiled.dateTimeStates);
    needLocaltime_ = compiled.needLocaltime;
}

void PatternFormatter::format(const details::LogMsg &msg, std::string &dst)
{
    if (program_.size() == 1 && program_.front().code == OpCode::MESSAGE) {
        const auto requiredCapacity = dst.size() + msg.message.size() + eol_.size();
        if (dst.capacity() < requiredCapacity) {
            dst.reserve(requiredCapacity);
        }
        dst.append(msg.message);
        dst.append(eol_);
        return;
    }

    const auto requiredCapacity = dst.size() + msg.loggerName.size() + msg.message.size() +
                                  msg.source.file.size() + msg.source.function.size() + eol_.size() + 96;
    if (dst.capacity() < requiredCapacity) {
        dst.reserve(requiredCapacity);
    }

    if (needLocaltime_) {
        const auto seconds = std::chrono::floor<std::chrono::seconds>(msg.timestamp.time_since_epoch());
        if (!cachedTmValid_ || seconds != lastLogSecs_) {
            cachedTm_ = getTime(SystemTimePoint{seconds});
            lastLogSecs_ = seconds;
            cachedTmValid_ = true;
        }
    }

    for (const auto &operation : program_) {
        switch (operation.code) {
            case OpCode::LITERAL:
                dst.append(literals_.data() + operation.argument, operation.size);
                break;
            case OpCode::FULL:
                formatFull(msg, cachedTm_, fullStates_[operation.argument], dst);
                break;
            case OpCode::NAME:
                dst.append(msg.loggerName);
                break;
            case OpCode::LEVEL:
                dst.append(LevelToString(msg.level));
                break;
            case OpCode::THREAD_ID:
                AppendInteger(msg.threadId, dst);
                break;
            case OpCode::MESSAGE:
                dst.append(msg.message);
                break;
            case OpCode::WEEKDAY_SHORT:
                dst.append(kDays[static_cast<std::size_t>(cachedTm_.tm_wday)]);
                break;
            case OpCode::WEEKDAY_FULL:
                dst.append(kFullDays[static_cast<std::size_t>(cachedTm_.tm_wday)]);
                break;
            case OpCode::MONTH_SHORT:
                dst.append(kMonths[static_cast<std::size_t>(cachedTm_.tm_mon)]);
                break;
            case OpCode::MONTH_FULL:
                dst.append(kFullMonths[static_cast<std::size_t>(cachedTm_.tm_mon)]);
                break;
            case OpCode::DATE_TIME:
                dst.append(kDays[static_cast<std::size_t>(cachedTm_.tm_wday)]);
                dst.push_back(' ');
                dst.append(kMonths[static_cast<std::size_t>(cachedTm_.tm_mon)]);
                dst.push_back(' ');
                AppendIntegerPadded(cachedTm_.tm_mday, 2, dst);
                dst.push_back(' ');
                AppendIntegerPadded(cachedTm_.tm_hour, 2, dst);
                dst.push_back(':');
                AppendIntegerPadded(cachedTm_.tm_min, 2, dst);
                dst.push_back(':');
                AppendIntegerPadded(cachedTm_.tm_sec, 2, dst);
                dst.push_back(' ');
                AppendInteger(cachedTm_.tm_year + 1900, dst);
                break;
            case OpCode::COMMON_DATE_TIME:
                formatCommonDateTime(msg, cachedTm_, dateTimeStates_[operation.argument], operation.size != 0,
                                     dst);
                break;
            case OpCode::YEAR_SHORT:
                AppendIntegerPadded(cachedTm_.tm_year % 100, 2, dst);
                break;
            case OpCode::YEAR:
                AppendInteger(cachedTm_.tm_year + 1900, dst);
                break;
            case OpCode::DATE_SHORT:
                AppendIntegerPadded(cachedTm_.tm_mon + 1, 2, dst);
                dst.push_back('/');
                AppendIntegerPadded(cachedTm_.tm_mday, 2, dst);
                dst.push_back('/');
                AppendIntegerPadded(cachedTm_.tm_year % 100, 2, dst);
                break;
            case OpCode::MONTH:
                AppendIntegerPadded(cachedTm_.tm_mon + 1, 2, dst);
                break;
            case OpCode::DAY:
                AppendIntegerPadded(cachedTm_.tm_mday, 2, dst);
                break;
            case OpCode::HOUR_24:
                AppendIntegerPadded(cachedTm_.tm_hour, 2, dst);
                break;
            case OpCode::HOUR_12:
                AppendIntegerPadded(To12Hour(cachedTm_), 2, dst);
                break;
            case OpCode::MINUTE:
                AppendIntegerPadded(cachedTm_.tm_min, 2, dst);
                break;
            case OpCode::SECOND:
                AppendIntegerPadded(cachedTm_.tm_sec, 2, dst);
                break;
            case OpCode::MILLISECOND:
                AppendIntegerPadded(FractionalPart<std::chrono::milliseconds>(msg.timestamp), 3, dst);
                break;
            case OpCode::MICROSECOND:
                AppendIntegerPadded(FractionalPart<std::chrono::microseconds>(msg.timestamp), 6, dst);
                break;
            case OpCode::NANOSECOND:
                AppendIntegerPadded(FractionalPart<std::chrono::nanoseconds>(msg.timestamp), 9, dst);
                break;
            case OpCode::EPOCH_SECOND:
                AppendInteger(
                    std::chrono::duration_cast<std::chrono::seconds>(msg.timestamp.time_since_epoch())
                        .count(),
                    dst);
                break;
            case OpCode::AM_PM:
                dst.append(AmPm(cachedTm_));
                break;
            case OpCode::CLOCK_12:
                AppendIntegerPadded(To12Hour(cachedTm_), 2, dst);
                dst.push_back(':');
                AppendIntegerPadded(cachedTm_.tm_min, 2, dst);
                dst.push_back(':');
                AppendIntegerPadded(cachedTm_.tm_sec, 2, dst);
                dst.push_back(' ');
                dst.append(AmPm(cachedTm_));
                break;
            case OpCode::CLOCK_24:
                AppendIntegerPadded(cachedTm_.tm_hour, 2, dst);
                dst.push_back(':');
                AppendIntegerPadded(cachedTm_.tm_min, 2, dst);
                break;
            case OpCode::CLOCK_ISO:
                AppendIntegerPadded(cachedTm_.tm_hour, 2, dst);
                dst.push_back(':');
                AppendIntegerPadded(cachedTm_.tm_min, 2, dst);
                dst.push_back(':');
                AppendIntegerPadded(cachedTm_.tm_sec, 2, dst);
                break;
            case OpCode::PID:
                AppendInteger(static_cast<Uint32>(core::Pid()), dst);
                break;
            case OpCode::SOURCE_LOCATION:
                if (!msg.source.empty()) {
                    dst.append(msg.source.file);
                    dst.push_back(':');
                    AppendInteger(msg.source.line, dst);
                }
                break;
            case OpCode::SOURCE_FILENAME_SHORT:
                if (!msg.source.empty()) {
                    dst.append(Basename(msg.source.file));
                }
                break;
            case OpCode::SOURCE_FILENAME:
                if (!msg.source.empty()) {
                    dst.append(msg.source.file);
                }
                break;
            case OpCode::SOURCE_LINE:
                if (!msg.source.empty()) {
                    AppendInteger(msg.source.line, dst);
                }
                break;
            case OpCode::SOURCE_FUNCTION:
                if (!msg.source.empty()) {
                    dst.append(msg.source.function);
                }
                break;
            case OpCode::ELAPSED_NANOSECOND:
            case OpCode::ELAPSED_MICROSECOND:
            case OpCode::ELAPSED_MILLISECOND:
            case OpCode::ELAPSED_SECOND: {
                auto &state = elapsedStates_[operation.argument];
                if (!state.initialized) {
                    state.initialized = true;
                    state.lastMessageTime = msg.timestamp;
                    dst.push_back('0');
                    break;
                }

                const auto delta =
                    (std::max)(msg.timestamp - state.lastMessageTime, SystemClock::duration::zero());
                state.lastMessageTime = msg.timestamp;
                switch (operation.code) {
                    case OpCode::ELAPSED_NANOSECOND:
                        AppendInteger(std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count(),
                                      dst);
                        break;
                    case OpCode::ELAPSED_MICROSECOND:
                        AppendInteger(std::chrono::duration_cast<std::chrono::microseconds>(delta).count(),
                                      dst);
                        break;
                    case OpCode::ELAPSED_MILLISECOND:
                        AppendInteger(std::chrono::duration_cast<std::chrono::milliseconds>(delta).count(),
                                      dst);
                        break;
                    case OpCode::ELAPSED_SECOND:
                        AppendInteger(std::chrono::duration_cast<std::chrono::seconds>(delta).count(), dst);
                        break;
                    default:
                        break;
                }
                break;
            }
        }
    }

    dst.append(eol_);
}

std::unique_ptr<IFormatter> PatternFormatter::clone() const
{
    auto cloned = std::make_unique<PatternFormatter>(pattern_, patternTimeType_, eol_);
#if defined(__GNUC__) && __GNUC__ < 5
    return std::move(cloned);
#else
    return cloned;
#endif
}

void PatternFormatter::setPattern(std::string pattern)
{
    auto compiled = compilePattern(pattern);

    pattern_ = std::move(pattern);
    program_ = std::move(compiled.program);
    literals_ = std::move(compiled.literals);
    elapsedStates_ = std::move(compiled.elapsedStates);
    fullStates_ = std::move(compiled.fullStates);
    dateTimeStates_ = std::move(compiled.dateTimeStates);
    needLocaltime_ = compiled.needLocaltime;
    cachedTmValid_ = false;
}

std::tm PatternFormatter::getTime(SystemTimePoint timestamp) const
{
    if (patternTimeType_ == PatternTimeType::LOCAL) {
        return core::Localtime(SystemClock::to_time_t(timestamp));
    }
    return core::Gmtime(SystemClock::to_time_t(timestamp));
}

void PatternFormatter::formatFull(const details::LogMsg &msg, const std::tm &time, FullState &state,
                                  std::string &dst)
{
    const auto seconds = lastLogSecs_;
    if (!state.initialized || state.cachedSecond != seconds) {
        state.cachedDatetime.clear();
        state.cachedDatetime.push_back('[');
        AppendInteger(time.tm_year + 1900, state.cachedDatetime);
        state.cachedDatetime.push_back('-');
        AppendIntegerPadded(time.tm_mon + 1, 2, state.cachedDatetime);
        state.cachedDatetime.push_back('-');
        AppendIntegerPadded(time.tm_mday, 2, state.cachedDatetime);
        state.cachedDatetime.push_back(' ');
        AppendIntegerPadded(time.tm_hour, 2, state.cachedDatetime);
        state.cachedDatetime.push_back(':');
        AppendIntegerPadded(time.tm_min, 2, state.cachedDatetime);
        state.cachedDatetime.push_back(':');
        AppendIntegerPadded(time.tm_sec, 2, state.cachedDatetime);
        state.cachedDatetime.push_back('.');
        state.cachedSecond = seconds;
        state.initialized = true;
    }

    char threadIdBuffer[32];
    const auto threadIdResult =
        std::to_chars(std::begin(threadIdBuffer), std::end(threadIdBuffer), msg.threadId);
    const auto threadIdSize = static_cast<std::size_t>(threadIdResult.ptr - threadIdBuffer);

    char sourceLineBuffer[32];
    std::size_t sourceLineSize = 0;
    const auto hasSource = !msg.source.empty();
    if (hasSource) {
        const auto sourceLineResult =
            std::to_chars(std::begin(sourceLineBuffer), std::end(sourceLineBuffer), msg.source.line);
        sourceLineSize = static_cast<std::size_t>(sourceLineResult.ptr - sourceLineBuffer);
    }

    const auto level = LevelToString(msg.level);
    constexpr std::string_view kCloseOpen{"] ["};
    constexpr std::string_view kThreadPrefix{"] [tid="};
    constexpr std::string_view kCloseSpace{"] "};
    const auto sourceSize =
        hasSource ? msg.source.file.size() + sourceLineSize + 4 : static_cast<std::size_t>(0);
    const auto appendSize = state.cachedDatetime.size() + 3 + kCloseOpen.size() + level.size() +
                            kCloseOpen.size() + msg.loggerName.size() + kThreadPrefix.size() + threadIdSize +
                            kCloseSpace.size() + sourceSize + msg.message.size();

    const auto originalSize = dst.size();
    dst.resize(originalSize + appendSize);
    auto *cursor = dst.data() + originalSize;
    auto copyField = [&cursor](std::string_view field) {
        if (!field.empty()) {
            std::memcpy(cursor, field.data(), field.size());
            cursor += field.size();
        }
    };

    copyField(state.cachedDatetime);
    const auto milliseconds = static_cast<Uint32>(
        std::chrono::duration_cast<std::chrono::milliseconds>(msg.timestamp.time_since_epoch() - seconds)
            .count());
    WriteThreeDigits(milliseconds, cursor);
    cursor += 3;
    copyField(kCloseOpen);
    copyField(level);
    copyField(kCloseOpen);
    copyField(msg.loggerName);
    copyField(kThreadPrefix);
    copyField(std::string_view(threadIdBuffer, threadIdSize));
    copyField(kCloseSpace);
    if (hasSource) {
        *cursor++ = '[';
        copyField(msg.source.file);
        *cursor++ = ':';
        copyField(std::string_view(sourceLineBuffer, sourceLineSize));
        copyField(kCloseSpace);
    }
    copyField(msg.message);
}

void PatternFormatter::formatCommonDateTime(const details::LogMsg &msg, const std::tm &time,
                                            DateTimeState &state, bool withMilliseconds, std::string &dst)
{
    const auto seconds = lastLogSecs_;
    if (!state.initialized || state.cachedSecond != seconds) {
        state.cachedPrefix.clear();
        AppendInteger(time.tm_year + 1900, state.cachedPrefix);
        state.cachedPrefix.push_back('-');
        AppendIntegerPadded(time.tm_mon + 1, 2, state.cachedPrefix);
        state.cachedPrefix.push_back('-');
        AppendIntegerPadded(time.tm_mday, 2, state.cachedPrefix);
        state.cachedPrefix.push_back(' ');
        AppendIntegerPadded(time.tm_hour, 2, state.cachedPrefix);
        state.cachedPrefix.push_back(':');
        AppendIntegerPadded(time.tm_min, 2, state.cachedPrefix);
        state.cachedPrefix.push_back(':');
        AppendIntegerPadded(time.tm_sec, 2, state.cachedPrefix);
        if (withMilliseconds) {
            state.cachedPrefix.push_back('.');
        }
        state.cachedSecond = seconds;
        state.initialized = true;
    }
    dst.append(state.cachedPrefix);
    if (withMilliseconds) {
        AppendIntegerPadded(
            std::chrono::duration_cast<std::chrono::milliseconds>(msg.timestamp.time_since_epoch() - seconds)
                .count(),
            3, dst);
    }
}

PatternFormatter::CompiledPattern PatternFormatter::compilePattern(const std::string &pattern) const
{
    CompiledPattern compiled;
    compiled.program.reserve(pattern.size() / 2 + 1);
    compiled.literals.reserve(pattern.size());

    constexpr auto kMaxOperand = (std::numeric_limits<Uint32>::max)();
    auto checkedOperand = [](std::size_t value) {
        if (value > kMaxOperand) {
            NOVAINFRA_THROW(std::length_error("Log pattern exceeds the 32-bit opcode limit"));
        }
        return static_cast<Uint32>(value);
    };

    std::string pendingLiteral;
    pendingLiteral.reserve(pattern.size());
    auto flushLiteral = [&] {
        if (pendingLiteral.empty()) {
            return;
        }
        if (pendingLiteral.size() > kMaxOperand - compiled.literals.size()) {
            NOVAINFRA_THROW(std::length_error("Log pattern literal storage exceeds the 32-bit opcode limit"));
        }
        const auto offset = checkedOperand(compiled.literals.size());
        const auto size = checkedOperand(pendingLiteral.size());
        compiled.literals.append(pendingLiteral);
        compiled.program.push_back(Operation{OpCode::LITERAL, offset, size});
        pendingLiteral.clear();
    };

    for (std::size_t index = 0; index < pattern.size(); ++index) {
        if (pattern[index] != '%') {
            pendingLiteral.push_back(pattern[index]);
            continue;
        }
        if (++index == pattern.size()) {
            NOVAINFRA_THROW(std::invalid_argument("Log pattern must not end with '%'"));
        }

        --index;
        const auto remaining = pattern.size() - index;
        const auto isCommonWithMillis =
            remaining >= kCommonDateTimeMillis.size() &&
            pattern.compare(index, kCommonDateTimeMillis.size(), kCommonDateTimeMillis.data(),
                            kCommonDateTimeMillis.size()) == 0;
        const auto isCommon = !isCommonWithMillis && remaining >= kCommonDateTime.size() &&
                              pattern.compare(index, kCommonDateTime.size(), kCommonDateTime.data(),
                                              kCommonDateTime.size()) == 0;
        if (isCommonWithMillis || isCommon) {
            flushLiteral();
            const auto stateIndex = checkedOperand(compiled.dateTimeStates.size());
            compiled.dateTimeStates.emplace_back();
            compiled.dateTimeStates.back().cachedPrefix.reserve(24);
            compiled.program.push_back(
                Operation{OpCode::COMMON_DATE_TIME, stateIndex, isCommonWithMillis ? 3U : 0U});
            compiled.needLocaltime = true;
            index += (isCommonWithMillis ? kCommonDateTimeMillis.size() : kCommonDateTime.size()) - 1;
            continue;
        }
        ++index;

        const auto flag = pattern[index];
        if (flag == '%') {
            pendingLiteral.push_back('%');
            continue;
        }
        flushLiteral();

        Operation operation{OpCode::MESSAGE, 0, 0};
        switch (flag) {
            case '+':
                operation.code = OpCode::FULL;
                operation.argument = checkedOperand(compiled.fullStates.size());
                compiled.fullStates.emplace_back();
                compiled.fullStates.back().cachedDatetime.reserve(24);
                compiled.needLocaltime = true;
                break;
            case 'n':
                operation.code = OpCode::NAME;
                break;
            case 'l':
                operation.code = OpCode::LEVEL;
                break;
            case 't':
                operation.code = OpCode::THREAD_ID;
                break;
            case 'v':
                operation.code = OpCode::MESSAGE;
                break;
            case 'a':
                operation.code = OpCode::WEEKDAY_SHORT;
                compiled.needLocaltime = true;
                break;
            case 'A':
                operation.code = OpCode::WEEKDAY_FULL;
                compiled.needLocaltime = true;
                break;
            case 'b':
            case 'h':
                operation.code = OpCode::MONTH_SHORT;
                compiled.needLocaltime = true;
                break;
            case 'B':
                operation.code = OpCode::MONTH_FULL;
                compiled.needLocaltime = true;
                break;
            case 'c':
                operation.code = OpCode::DATE_TIME;
                compiled.needLocaltime = true;
                break;
            case 'C':
                operation.code = OpCode::YEAR_SHORT;
                compiled.needLocaltime = true;
                break;
            case 'Y':
                operation.code = OpCode::YEAR;
                compiled.needLocaltime = true;
                break;
            case 'D':
            case 'x':
                operation.code = OpCode::DATE_SHORT;
                compiled.needLocaltime = true;
                break;
            case 'm':
                operation.code = OpCode::MONTH;
                compiled.needLocaltime = true;
                break;
            case 'd':
                operation.code = OpCode::DAY;
                compiled.needLocaltime = true;
                break;
            case 'H':
                operation.code = OpCode::HOUR_24;
                compiled.needLocaltime = true;
                break;
            case 'I':
                operation.code = OpCode::HOUR_12;
                compiled.needLocaltime = true;
                break;
            case 'M':
                operation.code = OpCode::MINUTE;
                compiled.needLocaltime = true;
                break;
            case 'S':
                operation.code = OpCode::SECOND;
                compiled.needLocaltime = true;
                break;
            case 'e':
                operation.code = OpCode::MILLISECOND;
                break;
            case 'f':
                operation.code = OpCode::MICROSECOND;
                break;
            case 'F':
                operation.code = OpCode::NANOSECOND;
                break;
            case 'E':
                operation.code = OpCode::EPOCH_SECOND;
                break;
            case 'p':
                operation.code = OpCode::AM_PM;
                compiled.needLocaltime = true;
                break;
            case 'r':
                operation.code = OpCode::CLOCK_12;
                compiled.needLocaltime = true;
                break;
            case 'R':
                operation.code = OpCode::CLOCK_24;
                compiled.needLocaltime = true;
                break;
            case 'T':
            case 'X':
                operation.code = OpCode::CLOCK_ISO;
                compiled.needLocaltime = true;
                break;
            case 'P':
                operation.code = OpCode::PID;
                break;
            case '@':
                operation.code = OpCode::SOURCE_LOCATION;
                break;
            case 's':
                operation.code = OpCode::SOURCE_FILENAME_SHORT;
                break;
            case 'g':
                operation.code = OpCode::SOURCE_FILENAME;
                break;
            case '#':
                operation.code = OpCode::SOURCE_LINE;
                break;
            case '!':
                operation.code = OpCode::SOURCE_FUNCTION;
                break;
            case 'u':
                operation.code = OpCode::ELAPSED_NANOSECOND;
                operation.argument = checkedOperand(compiled.elapsedStates.size());
                compiled.elapsedStates.emplace_back();
                break;
            case 'i':
                operation.code = OpCode::ELAPSED_MICROSECOND;
                operation.argument = checkedOperand(compiled.elapsedStates.size());
                compiled.elapsedStates.emplace_back();
                break;
            case 'o':
                operation.code = OpCode::ELAPSED_MILLISECOND;
                operation.argument = checkedOperand(compiled.elapsedStates.size());
                compiled.elapsedStates.emplace_back();
                break;
            case 'O':
                operation.code = OpCode::ELAPSED_SECOND;
                operation.argument = checkedOperand(compiled.elapsedStates.size());
                compiled.elapsedStates.emplace_back();
                break;
            default:
                NOVAINFRA_THROW(std::invalid_argument(std::string("Unsupported log pattern flag: %") + flag));
                break;
        }
        compiled.program.push_back(operation);
    }
    flushLiteral();
    return compiled;
}

}  // namespace log::formatters
NOVAINFRA_NAMESPACE_END
