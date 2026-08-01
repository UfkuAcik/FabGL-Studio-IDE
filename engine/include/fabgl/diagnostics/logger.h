#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace fabgl {

enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
};

using LogValue = std::variant<bool, std::int64_t, std::uint64_t, double, std::string>;

struct LogField final {
    std::string key;
    LogValue value;
};

struct LogRecord final {
    std::uint64_t sequence = 0;
    std::chrono::system_clock::time_point timestamp;
    std::uint64_t threadId = 0;
    LogLevel level = LogLevel::Info;
    std::string category;
    std::string message;
    std::vector<LogField> fields;
};

class ILogSink {
  public:
    virtual ~ILogSink() = default;
    virtual void write(const LogRecord& record) = 0;
};

class Logger final {
  public:
    void setMinimumLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel minimumLevel() const noexcept;

    void addSink(std::shared_ptr<ILogSink> sink);
    void clearSinks();

    void log(LogLevel level, std::string_view category, std::string_view message,
             std::initializer_list<LogField> fields = {});

  private:
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<ILogSink>> sinks_;
    std::atomic<std::uint64_t> nextSequence_{1};
    std::atomic<LogLevel> minimumLevel_{LogLevel::Info};
};

class MemoryLogSink final : public ILogSink {
  public:
    void write(const LogRecord& record) override;
    [[nodiscard]] std::vector<LogRecord> records() const;
    void clear();

  private:
    mutable std::mutex mutex_;
    std::vector<LogRecord> records_;
};

class JsonLineLogSink final : public ILogSink {
  public:
    explicit JsonLineLogSink(std::ostream& stream) : stream_(stream) {}
    void write(const LogRecord& record) override;

  private:
    std::ostream& stream_;
    std::mutex mutex_;
};

[[nodiscard]] std::string_view logLevelName(LogLevel level) noexcept;

} // namespace fabgl
