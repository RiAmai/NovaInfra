#include <logger/details/file_assistant.h>
#include <core/os_utils.h>

#include <cerrno>
#include <exception>
#include <stdexcept>
#include <system_error>
#include <utility>

NOVAINFRA_NAMESPACE_BEGIN
namespace log::details {

FileAssistant::~FileAssistant() NOVAINFRA_NOEXCEPT
{
    closeNoThrow();
}

void FileAssistant::ensureOpen(const char *operation) const
{
    if (fd_ == nullptr) {
        NOVAINFRA_THROW(std::runtime_error(std::string("Cannot ") + operation + " closed file " +
                                           core::FilenameToStr(filename_)));
    }
}

void FileAssistant::closeNoThrow() NOVAINFRA_NOEXCEPT
{
    NOVAINFRA_TRY
    {
        close();
    }
    NOVAINFRA_CATCH_STD_BODY(
        { std::fprintf(stderr, "[*** LOG FILE ERROR ***] close failed: %s\n", ex.what()); })
    NOVAINFRA_CATCH_ALL_BODY(
        { std::fputs("[*** LOG FILE ERROR ***] close failed with an unknown exception\n", stderr); })
}

void FileAssistant::open(const Filename &fname, bool truncate)
{
    close();
    filename_ = fname;

    auto *mode = NOVAINFRA_FILENAME("ab");
    auto *truncMode = NOVAINFRA_FILENAME("wb");

    if (eventHandlers_.beforeOpen) {
        eventHandlers_.beforeOpen(filename_);
    }
    for (int tries = 0; tries < openTries_; ++tries) {
        // create containing folder if not exists already.
        core::CreateDir(core::DirName(fname));
        if (truncate) {
            std::FILE *tmp;
            if (core::Fopen_s(&tmp, fname, truncMode)) {
                continue;
            }
            std::fclose(tmp);
        }
        if (!core::Fopen_s(&fd_, fname, mode)) {
            if (eventHandlers_.afterOpen) {
                NOVAINFRA_TRY
                {
                    eventHandlers_.afterOpen(filename_, fd_);
                }
                NOVAINFRA_CATCH_ALL_BODY({
                    auto *openedFile = std::exchange(fd_, nullptr);
                    std::fclose(openedFile);
                    NOVAINFRA_RETHROW_CURRENT();
                })
            }
            return;
        }

        core::SleepForMillis(openInterval_);
    }

    NOVAINFRA_THROW(
        std::system_error(std::error_code(errno, std::generic_category()),
                          "Failed opening file " + core::FilenameToStr(filename_) + " for writing"));
}

void FileAssistant::reopen(bool truncate)
{
    if (filename_.empty()) {
        NOVAINFRA_THROW(std::runtime_error("Failed reopening file: no file was opened previously"));
    }
    this->open(filename_, truncate);
}

void FileAssistant::flush()
{
    ensureOpen("flush");
    if (std::fflush(fd_) != 0) {
        NOVAINFRA_THROW(std::system_error(std::error_code(errno, std::generic_category()),
                                          "Failed flush to file " + core::FilenameToStr(filename_)));
    }
}

void FileAssistant::sync()
{
    ensureOpen("sync");
    if (!core::Fsync(fd_)) {
        NOVAINFRA_THROW(std::system_error(std::error_code(errno, std::generic_category()),
                                          "Failed to fsync file " + core::FilenameToStr(filename_)));
    }
}

void FileAssistant::close()
{
    if (fd_ == nullptr) {
        return;
    }

    auto *closingFile = std::exchange(fd_, nullptr);
    std::exception_ptr firstFailure;
    NOVAINFRA_TRY
    {
        if (eventHandlers_.beforeClose) {
            eventHandlers_.beforeClose(filename_, closingFile);
        }
    }
    NOVAINFRA_CATCH_ALL_BODY({ firstFailure = std::current_exception(); })

    const auto closeResult = std::fclose(closingFile);
    const auto closeError = errno;

    NOVAINFRA_TRY
    {
        if (eventHandlers_.afterClose) {
            eventHandlers_.afterClose(filename_);
        }
    }
    NOVAINFRA_CATCH_ALL_BODY({
        if (!firstFailure) {
            firstFailure = std::current_exception();
        }
    })

    if (firstFailure) {
        NOVAINFRA_RETHROW_EXCEPTION(firstFailure);
    }
    if (closeResult != 0) {
        NOVAINFRA_THROW(std::system_error(std::error_code(closeError, std::generic_category()),
                                          "Failed closing file " + core::FilenameToStr(filename_)));
    }
}

void FileAssistant::write(const std::string &buf)
{
    ensureOpen("write to");
    size_t msgSize = buf.size();
    auto data = buf.data();

    if (!core::FwriteBytes(data, msgSize, fd_)) {
        NOVAINFRA_THROW(std::system_error(std::error_code(errno, std::generic_category()),
                                          "Failed writing to file " + core::FilenameToStr(filename_)));
    }
}

size_t FileAssistant::size() const
{
    ensureOpen("get size of");
    return core::Filesize(fd_);
}

std::tuple<Filename, Filename> FileAssistant::splitByExtension(const Filename &fname)
{
    auto extIndex = fname.rfind('.');

    if (extIndex == Filename::npos || extIndex == 0 || extIndex == fname.size() - 1) {
        return std::make_tuple(fname, Filename());
    }

    auto folder_index = fname.find_last_of(core::kFolderSepsFilename);
    if (folder_index != Filename::npos && folder_index >= extIndex - 1) {
        return std::make_tuple(fname, Filename());
    }

    return std::make_tuple(fname.substr(0, extIndex), fname.substr(extIndex));
}

}  // namespace log::details
NOVAINFRA_NAMESPACE_END
