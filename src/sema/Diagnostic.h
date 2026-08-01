#pragma once
#include <string>
#include <sstream>

struct Diagnostic {
    enum class Level { Error, Warning };

    Level level;
    std::string message;
    std::string file;
    int line;
    int column;

    Diagnostic(Level level, std::string msg, std::string file, int line, int col)
        : level(level), message(std::move(msg)), file(std::move(file)), line(line), column(col) {}

    std::string format() const {
        std::ostringstream oss;
        oss << (level == Level::Error ? "error" : "warning") << ": " << message;
        if (!file.empty()) {
            oss << "\n  --> " << file << ":" << line << ":" << column;
        }
        return oss.str();
    }

    std::string formatWithSeverity() const {
        std::ostringstream oss;
        oss << (level == Level::Error ? "error" : "warning") << "[S" << (level == Level::Error ? "E" : "W") << "001]: " << message;
        if (!file.empty()) {
            oss << "\n  --> " << file << ":" << line << ":" << column;
        }
        return oss.str();
    }
};
