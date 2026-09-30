#pragma once

#include <core/export.h>
#include <core/namespace.h>
#include <core/macros.h>
#include <logger/sinks/file_event_handlers.h>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::details {

class NOVAINFRA_LIB_PORT FileAssistant
{
public:
    FileAssistant() = default;
    explicit FileAssistant(const sinks::FileEventHandlers &eventHandlers) : eventHandlers_(eventHandlers) {}
    NOVAINFRA_DISABLE_COPY(FileAssistant)
    ~FileAssistant() NOVAINFRA_NOEXCEPT;

    void open(const Filename &fname, bool truncate = false);
    void reopen(bool truncate);
    void flush();
    void sync();
    void close();
    void write(const std::string &buf);
    size_t size() const;
    const Filename &filename() const { return filename_; }

    static std::tuple<Filename, Filename> splitByExtension(const Filename &fname);

private:
    void ensureOpen(const char *operation) const;
    void closeNoThrow() NOVAINFRA_NOEXCEPT;

    const int openTries_ = 5;
    const unsigned int openInterval_ = 10;
    std::FILE *fd_{nullptr};
    Filename filename_;
    sinks::FileEventHandlers eventHandlers_;
};

}  // namespace log::details
NOVAINFRA_NAMESPACE_END
