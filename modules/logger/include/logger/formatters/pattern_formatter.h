#pragma once

#include <logger/formatters/formatter.h>

#include <core/macros.h>
#include <core/export.h>
#include <core/os_utils.h>

#include <vector>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::formatters {

enum class PatternTimeType {
    LOCAL,
    UTC,
};

class NOVAINFRA_LIB_PORT PatternFormatter NOVAINFRA_FINAL : public IFormatter
{
public:
    explicit PatternFormatter(PatternTimeType timeType = PatternTimeType::LOCAL,
                              std::string eol = core::kDefaultEol);
    explicit PatternFormatter(std::string pattern, PatternTimeType timeType = PatternTimeType::LOCAL,
                              std::string eol = core::kDefaultEol);
    NOVAINFRA_DISABLE_COPY(PatternFormatter)

    void format(const details::LogMsg &msg, std::string &dst) NOVAINFRA_OVERRIDE;
    std::unique_ptr<IFormatter> clone() const NOVAINFRA_OVERRIDE;
    void setPattern(std::string pattern);

private:
    enum class OpCode : Uint8 {
        LITERAL,
        FULL,
        NAME,
        LEVEL,
        THREAD_ID,
        MESSAGE,
        WEEKDAY_SHORT,
        WEEKDAY_FULL,
        MONTH_SHORT,
        MONTH_FULL,
        DATE_TIME,
        COMMON_DATE_TIME,
        YEAR_SHORT,
        YEAR,
        DATE_SHORT,
        MONTH,
        DAY,
        HOUR_24,
        HOUR_12,
        MINUTE,
        SECOND,
        MILLISECOND,
        MICROSECOND,
        NANOSECOND,
        EPOCH_SECOND,
        AM_PM,
        CLOCK_12,
        CLOCK_24,
        CLOCK_ISO,
        PID,
        SOURCE_LOCATION,
        SOURCE_FILENAME_SHORT,
        SOURCE_FILENAME,
        SOURCE_LINE,
        SOURCE_FUNCTION,
        ELAPSED_NANOSECOND,
        ELAPSED_MICROSECOND,
        ELAPSED_MILLISECOND,
        ELAPSED_SECOND,
    };

    struct Operation {
        OpCode code;
        Uint32 argument{0};
        Uint32 size{0};
    };

    struct ElapsedState {
        SystemTimePoint lastMessageTime{};
        bool initialized{false};
    };

    struct FullState {
        std::chrono::seconds cachedSecond{0};
        std::string cachedDatetime;
        bool initialized{false};
    };

    struct DateTimeState {
        std::chrono::seconds cachedSecond{0};
        std::string cachedPrefix;
        bool initialized{false};
    };

    struct CompiledPattern {
        std::vector<Operation> program;
        std::string literals;
        std::vector<ElapsedState> elapsedStates;
        std::vector<FullState> fullStates;
        std::vector<DateTimeState> dateTimeStates;
        bool needLocaltime{false};
    };

    std::tm getTime(SystemTimePoint timestamp) const;
    CompiledPattern compilePattern(const std::string &pattern) const;
    void formatFull(const details::LogMsg &msg, const std::tm &time, FullState &state, std::string &dst);
    void formatCommonDateTime(const details::LogMsg &msg, const std::tm &time, DateTimeState &state,
                              bool withMilliseconds, std::string &dst);

    std::string pattern_;
    std::vector<Operation> program_;
    std::string literals_;
    std::vector<ElapsedState> elapsedStates_;
    std::vector<FullState> fullStates_;
    std::vector<DateTimeState> dateTimeStates_;
    bool needLocaltime_{false};
    PatternTimeType patternTimeType_;
    std::chrono::seconds lastLogSecs_{0};
    std::tm cachedTm_{};
    bool cachedTmValid_{false};
    std::string eol_;
};
}  // namespace log::formatters
NOVAINFRA_NAMESPACE_END
