#include "File.h"
#include <sstream>

File::File() : m_mode(FileOpenMode::Read), m_isOpen(false) {}

File::File(const std::filesystem::path& filepath, FileOpenMode mode)
    : m_path(filepath), m_mode(mode), m_isOpen(false) {
    open(filepath, mode);
}

File::~File() {
    close();
}

File::File(File&& other) noexcept
    : m_path(std::move(other.m_path))
    , m_stream(std::move(other.m_stream))
    , m_mode(other.m_mode)
    , m_isOpen(other.m_isOpen) {
    other.m_isOpen = false;
}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();
        m_path = std::move(other.m_path);
        m_stream = std::move(other.m_stream);
        m_mode = other.m_mode;
        m_isOpen = other.m_isOpen;
        other.m_isOpen = false;
    }
    return *this;
}

bool File::open(const std::filesystem::path& filepath, FileOpenMode mode) {
    close();

    m_path = filepath;
    m_mode = mode;

    std::ios::openmode openMode = std::ios::binary;

    switch (mode) {
        case FileOpenMode::Read:
            openMode |= std::ios::in;
            break;
        case FileOpenMode::Write:
            openMode |= std::ios::out | std::ios::trunc;
            break;
        case FileOpenMode::Append:
            openMode |= std::ios::out | std::ios::app;
            break;
        case FileOpenMode::ReadWrite:
            openMode |= std::ios::in | std::ios::out;
            break;
    }

    m_stream.open(filepath, openMode);
    m_isOpen = m_stream.is_open();

    return m_isOpen;
}

void File::close() {
    if (m_stream.is_open()) {
        m_stream.close();
    }
    m_isOpen = false;
}

bool File::isOpen() const {
    return m_isOpen && m_stream.is_open();
}

File::operator bool() const {
    return isOpen();
}

std::string File::readAll() {
    if (!isOpen()) {
        return "";
    }

    std::ostringstream oss;
    oss << m_stream.rdbuf();
    return oss.str();
}

std::optional<std::string> File::readLine() {
    if (!isOpen()) {
        return std::nullopt;
    }

    std::string line;
    if (std::getline(m_stream, line)) {
        return line;
    }
    return std::nullopt;
}

std::vector<std::string> File::readLines() {
    std::vector<std::string> lines;

    if (!isOpen()) {
        return lines;
    }

    std::string line;
    while (std::getline(m_stream, line)) {
        lines.push_back(line);
    }

    return lines;
}

bool File::write(const std::string& content) {
    if (!isOpen()) {
        return false;
    }

    m_stream << content;
    return m_stream.good();
}

bool File::writeLine(const std::string& line) {
    if (!isOpen()) {
        return false;
    }

    m_stream << line << '\n';
    return m_stream.good();
}

bool File::writeLines(const std::vector<std::string>& lines) {
    if (!isOpen()) {
        return false;
    }

    for (const auto& line : lines) {
        m_stream << line << '\n';
        if (!m_stream.good()) {
            return false;
        }
    }

    return true;
}

bool File::append(const std::string& content) {
    if (!isOpen() || m_mode == FileOpenMode::Read) {
        return false;
    }

    m_stream.seekp(0, std::ios::end);
    m_stream << content;
    return m_stream.good();
}

bool File::appendLine(const std::string& line) {
    if (!isOpen() || m_mode == FileOpenMode::Read) {
        return false;
    }

    m_stream.seekp(0, std::ios::end);
    m_stream << line << '\n';
    return m_stream.good();
}

std::filesystem::path File::getPath() const {
    return m_path;
}

std::uintmax_t File::getSize() const {
    if (std::filesystem::exists(m_path)) {
        return std::filesystem::file_size(m_path);
    }
    return 0;
}

bool File::exists() const {
    return std::filesystem::exists(m_path);
}

bool File::exists(const std::filesystem::path& filepath) {
    return std::filesystem::exists(filepath);
}

std::uintmax_t File::tellReadPos() {
    if (!isOpen()) {
        return 0;
    }
    return static_cast<std::uintmax_t>(m_stream.tellg());
}

std::uintmax_t File::tellWritePos() {
    if (!isOpen()) {
        return 0;
    }
    return static_cast<std::uintmax_t>(m_stream.tellp());
}

bool File::seekReadPos(std::uintmax_t pos) {
    if (!isOpen()) {
        return false;
    }
    m_stream.seekg(static_cast<std::streampos>(pos));
    return m_stream.good();
}

bool File::seekWritePos(std::uintmax_t pos) {
    if (!isOpen()) {
        return false;
    }
    m_stream.seekp(static_cast<std::streampos>(pos));
    return m_stream.good();
}

bool File::isEndOfFile() const {
    if (!isOpen()) {
        return true;
    }
    return m_stream.eof();
}
