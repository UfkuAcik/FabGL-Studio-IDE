#include "fabgl/diagnostics/logger.h"

#include <functional>
#include <iomanip>
#include <sstream>
#include <thread>
#include <type_traits>

namespace fabgl {
namespace {

std::string escapeJson(std::string_view value) {
    std::ostringstream stream;
    for (const auto character : value) {
        switch (character) {
        case '\\':
            stream << "\\\\";
            break;
        case '"':
            stream << "\\\"";
            break;
        case '\n':
            stream << "\\n";
            break;
        case '\r':
            stream << "\\r";
            break;
        case '\t':
            stream << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(static_cast<unsigned char>(character));
            } else {
                stream << character;
            }
            break;
        }
    }
    return stream.str();
}

void writeJsonValue(std::ostream& stream, const LogValue& value) {
    std::visit(
        [&stream](const auto& typedValue) {
            using ValueType = std::decay_t<decltype(typedValue)>;
            if constexpr (std::is_same_v<ValueType, std::string>) {
                stream << '"' << escapeJson(typedValue) << '"';
            } else if constexpr (std::is_same_v<ValueType, bool>) {
                stream << (typedValue ? "true" : "false");
            } else {
                stream << typedValue;
            }
        },
        value);
}

} // namespace

void Logger::setMinimumLevel(LogLevel level) noexcept {
    minimumLevel_.store(level, std::memory_order_relaxed);
}

LogLevel Logger::minimumLevel() const noexcept {
    return minimumLevel_.load(std::memory_order_relaxed);
}

void Logger::addSink(std::shared_ptr<ILogSink> sink) {
    if (!sink) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::clearSinks() {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
}

void Logger::log(LogLevel level, std::string_view category, std::string_view message,
                 std::initializer_list<LogField> fields) {
    if (static_cast<int>(level) < static_cast<int>(minimumLevel())) {
        return;
    }

    LogRecord record;
    record.sequence = nextSequence_.fetch_add(1, std::memory_order_relaxed);
    record.timestamp = std::chrono::system_clock::now();
    record.threadId =
        static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    record.level = level;
    record.category = std::string(category);
    record.message = std::string(message);
    record.fields.assign(fields.begin(), fields.end());

    std::vector<std::shared_ptr<ILogSink>> sinks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sinks = sinks_;
    }
    for (const auto& sink : sinks) {
        sink->write(record);
    }
}

void MemoryLogSink::write(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.push_back(record);
}

std::vector<LogRecord> MemoryLogSink::records() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return records_;
}

void MemoryLogSink::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.clear();
}

void JsonLineLogSink::write(const LogRecord& record) {
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(record.timestamp.time_since_epoch())
            .count();

    std::lock_guard<std::mutex> lock(mutex_);
    stream_ << "{\"sequence\":" << record.sequence << ",\"timestamp_ms\":" << milliseconds
            << ",\"thread_id\":" << record.threadId << ",\"level\":\"" << logLevelName(record.level)
            << "\",\"category\":\"" << escapeJson(record.category) << "\",\"message\":\""
            << escapeJson(record.message) << "\",\"fields\":{";
    for (std::size_t index = 0; index < record.fields.size(); ++index) {
        if (index != 0) {
            stream_ << ',';
        }
        stream_ << '"' << escapeJson(record.fields[index].key) << "\":";
        writeJsonValue(stream_, record.fields[index].value);
    }
    stream_ << "}}\n";
}

std::string_view logLevelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    case LogLevel::Critical:
        return "critical";
    }
    return "unknown";
}

} // namespace fabgl
