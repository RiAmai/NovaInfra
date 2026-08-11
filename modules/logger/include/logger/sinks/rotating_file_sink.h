#pragma once

#include <logger/details/file_assistant.h>
#include <logger/details/mutex.h>
#include <logger/sinks/base_sink.h>
#include <logger/sinks/file_event_handlers.h>

#include <fmt/format.h>

#include <cerrno>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <tuple>

NOVA_NAMESPACE_BEGIN
namespace log::sinks {

// rotating file sink
template <typename Mutex>
class RotatingFileSink : public BaseSink<Mutex>
{
public:
    static constexpr int kMaxFileNum = 200000;

    explicit RotatingFileSink(const Filename &filename, std::size_t maxSize, std::size_t maxFileNum,
                              bool rotateOnOpen = false, const FileEventHandlers &eventHandlers = {})
        : baseFilename_(filename), maxSize_(maxSize), maxFileNum_(maxFileNum), fileAssistant_{eventHandlers}
    {
        validateMaxSize(maxSize);
        validateMaxFileNum(maxFileNum);
        fileAssistant_.open(baseFilename_);
        curSize_ = fileAssistant_.size();
        if (rotateOnOpen && curSize_ > 0) {
            rotate();
        }
    }

    static Filename calcFilename(const Filename &name, std::size_t idx)
    {
        if (idx == 0U) {
            return name;
        }

        Filename basename;
        Filename ext;
        std::tie(basename, ext) = details::FileAssistant::splitByExtension(name);
        return fmt::format(FMT_STRING(NOVA_FILENAME("{}.{}{}")), basename, idx, ext);
    }

    Filename filename() const
    {
        std::lock_guard<Mutex> lock(BaseSink<Mutex>::mutex_);
        return fileAssistant_.filename();
    }

    void rotateNow()
    {
        std::lock_guard<Mutex> lock(BaseSink<Mutex>::mutex_);
        rotate();
    }

    void setMaxSize(std::size_t size)
    {
        validateMaxSize(size);
        std::lock_guard<Mutex> lock(BaseSink<Mutex>::mutex_);
        maxSize_ = size;
    }
    std::size_t getMaxSize() const
    {
        std::lock_guard<Mutex> lock(BaseSink<Mutex>::mutex_);
        return maxSize_;
    }

    void setMaxFileNum(std::size_t size)
    {
        validateMaxFileNum(size);
        std::lock_guard<Mutex> lock(BaseSink<Mutex>::mutex_);
        maxFileNum_ = size;
    }
    std::size_t getMaxFileNum() const
    {
        std::lock_guard<Mutex> lock(BaseSink<Mutex>::mutex_);
        return maxFileNum_;
    }

protected:
    void _log(const details::LogMsg &msg) final override
    {
        const auto &formatted = BaseSink<Mutex>::formatMessage(msg);

        const auto exceedsLimit = curSize_ > maxSize_ || formatted.size() > maxSize_ - curSize_;
        std::size_t newSize;
        if (exceedsLimit) {
            fileAssistant_.flush();
            if (fileAssistant_.size() > 0) {
                rotate();
            } else {
                curSize_ = 0;
            }
            newSize = formatted.size();
        } else {
            newSize = curSize_ + formatted.size();
        }
        fileAssistant_.write(formatted);
        curSize_ = newSize;
    }
    void _flush() final override { fileAssistant_.flush(); }

private:
    static void validateMaxSize(std::size_t size)
    {
        if (size == 0) {
            NOVA_THROW(std::invalid_argument("Rotating sink max size must be greater than zero"));
        }
    }

    static void validateMaxFileNum(std::size_t size)
    {
        if (size > static_cast<std::size_t>(kMaxFileNum)) {
            NOVA_THROW(std::invalid_argument("Rotating sink max file count exceeds kMaxFileNum"));
        }
    }

    /**
     * @brief Rotate files:
     *        log.txt -> log.1.txt
     *        log.1.txt -> log.2.txt
     *        log.2.txt -> log.3.txt
     *        log.3.txt -> delete
     * */
    void rotate()
    {
        fileAssistant_.close();

        for (std::size_t i = maxFileNum_; i > 0; --i) {
            Filename src = calcFilename(baseFilename_, i - 1);
            if (!core::PathExists(src)) {
                continue;
            }
            Filename dst = calcFilename(baseFilename_, i);
            if (!renameFile(src, dst)) {
                core::SleepForMillis(100);
                if (!renameFile(src, dst)) {
                    const auto renameError = std::error_code(errno, std::generic_category());
                    fileAssistant_.reopen(false);
                    curSize_ = fileAssistant_.size();
                    NOVA_THROW(std::system_error(renameError, "rotating_file_sink: failed renaming " +
                                                                  core::FilenameToStr(src) + " to " +
                                                                  core::FilenameToStr(dst)));
                }
            }
        }
        fileAssistant_.reopen(true);
        curSize_ = 0;
    }
    // delete the target if exists, and rename the src file to target
    // return true on success, false otherwise.
    bool renameFile(const Filename &src, const Filename &dst)
    {
        core::Remove(dst);
        return core::Rename(src, dst) == 0;
    }

    Filename baseFilename_;
    std::size_t maxSize_;  // one size = one byte
    std::size_t maxFileNum_;
    std::size_t curSize_{0};
    details::FileAssistant fileAssistant_;
};

using RotatingFileSinkMt = RotatingFileSink<std::mutex>;
using RotatingFileSinkSt = RotatingFileSink<details::NullMutex>;

}  // namespace log::sinks
NOVA_NAMESPACE_END
