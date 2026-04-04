#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <optional>

enum class FileOpenMode {
    Read,
    Write,
    Append,
    ReadWrite
};

class File {
public:
    File();
    explicit File(const std::filesystem::path& filepath, FileOpenMode mode = FileOpenMode::Read);
    ~File();

    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept;
    File& operator=(File&& other) noexcept;

    bool open(const std::filesystem::path& filepath, FileOpenMode mode = FileOpenMode::Read);
    void close();
    bool isOpen() const;
    explicit operator bool() const;

    std::string readAll();
    std::optional<std::string> readLine();
    std::vector<std::string> readLines();

    bool write(const std::string& content);
    bool writeLine(const std::string& line);
    bool writeLines(const std::vector<std::string>& lines);

    bool append(const std::string& content);
    bool appendLine(const std::string& line);

    std::filesystem::path getPath() const;
    std::uintmax_t getSize() const;
    bool exists() const;
    static bool exists(const std::filesystem::path& filepath);

    std::uintmax_t tellReadPos();
    std::uintmax_t tellWritePos();
    bool seekReadPos(std::uintmax_t pos);
    bool seekWritePos(std::uintmax_t pos);

    bool isEndOfFile() const;

private:
    std::filesystem::path m_path;
    std::fstream m_stream;
    FileOpenMode m_mode;
    bool m_isOpen;
};
