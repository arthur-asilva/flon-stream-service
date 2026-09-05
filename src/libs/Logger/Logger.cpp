#include "Logger.hpp"

#include <filesystem>
#include <fstream>
#include <ctime>

namespace
{
// Timestamp legivel para prefixar cada linha do log. Chamado
// sempre com Logger::mutex_ ja travado (std::localtime usa um
// buffer interno estatico, nao e thread-safe por conta propria).
std::string currentTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    const std::tm* tmPtr = std::localtime(&nowTime);

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tmPtr);

    return std::string(buffer);
}
}

Logger::Logger()
{
    const std::string recordsDir =
        std::string(PROJECT_ROOT_DIR) +
        "/records";

    std::filesystem::create_directories(recordsDir);

    globalLogPath_ = recordsDir + "/system.log";
}

Logger& Logger::instance()
{
    static Logger logger;

    return logger;
}

void Logger::setActiveSessionDir(const std::string& dir)
{
    std::lock_guard<std::mutex> lock(mutex_);

    activeSessionDir_ = dir;
}

void Logger::clearActiveSessionDir()
{
    std::lock_guard<std::mutex> lock(mutex_);

    activeSessionDir_.clear();
}

void Logger::logError(const std::string& message)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string line =
        "[" + currentTimestamp() + "] " + message + "\n";

    std::ofstream globalFile(globalLogPath_, std::ios::app);

    if (globalFile.is_open())
    {
        globalFile << line;
    }

    if (!activeSessionDir_.empty())
    {
        std::ofstream sessionFile(
            activeSessionDir_ + "/error.log",
            std::ios::app
        );

        if (sessionFile.is_open())
        {
            sessionFile << line;
        }
    }

    std::cerr << line;
}
