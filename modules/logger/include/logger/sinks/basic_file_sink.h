#pragma once

#include <logger/details/file_assistant.h>
#include <logger/details/mutex.h>
#include <logger/sinks/base_sink.h>
#include <logger/sinks/file_event_handlers.h>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::sinks {

// basic file sink
template <typename Mutex>
class BasicFileSink : public BaseSink<Mutex>
{
public:
    explicit BasicFileSink(const Filename &filename, bool truncate = false,
                           const FileEventHandlers &eventHandlers = {})
        : fileAssistant_{eventHandlers}
    {
        fileAssistant_.open(filename, truncate);
    }
    Filename filename() const
    {
        std::lock_guard<Mutex> lock(BaseSink<Mutex>::mutex_);
        return fileAssistant_.filename();
    }
    void truncate()
    {
        std::lock_guard<Mutex> lock(BaseSink<Mutex>::mutex_);
        fileAssistant_.reopen(true);
    }

protected:
    void _log(const details::LogMsg &msg) NOVAINFRA_FINAL NOVAINFRA_OVERRIDE
    {
        fileAssistant_.write(BaseSink<Mutex>::formatMessage(msg));
    }
    void _flush() NOVAINFRA_FINAL NOVAINFRA_OVERRIDE { fileAssistant_.flush(); }

private:
    details::FileAssistant fileAssistant_;
};

using BasicFileSinkMt = BasicFileSink<std::mutex>;
using BasicFileSinkSt = BasicFileSink<details::NullMutex>;

}  // namespace log::sinks
NOVAINFRA_NAMESPACE_END
