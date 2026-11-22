module;

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>
#include <exception>
#include <format>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>

#include <sys/syscall.h>
#include <unistd.h>

export module CoreUtility:StderrLogger;

export namespace core_utility::stderr_logger {
enum class LogLevel : std::uint8_t { Info, Warning, Error, Critical };

enum class LogFormat : std::uint8_t { Text, Json };

struct LogField {
    enum class Kind : std::uint8_t { String, Integer, Unsigned, Boolean };

    std::string_view key;
    Kind kind{Kind::String};
    std::string_view text{};
    std::int64_t integer{0};
    std::uint64_t unsigned_value{0};
    bool boolean{false};

    LogField(const std::string_view k, const std::string_view v) noexcept
        : key(k)
        , text(v) {}
    LogField(const std::string_view k, const char* v) noexcept
        : key(k)
        , text(v != nullptr ? v : "") {}
    LogField(const std::string_view k, const bool v) noexcept
        : key(k)
        , kind(Kind::Boolean)
        , boolean(v) {}

    template <std::signed_integral T>
        requires(!std::same_as<T, bool>)
    LogField(const std::string_view k, const T v) noexcept
        : key(k)
        , kind(Kind::Integer)
        , integer(v) {}

    template <std::unsigned_integral T>
        requires(!std::same_as<T, bool>)
    LogField(const std::string_view k, const T v) noexcept
        : key(k)
        , kind(Kind::Unsigned)
        , unsigned_value(v) {}
};

class StderrLogger {
    static constexpr std::string_view kReset = "\033[0m";
    static constexpr std::string_view kGrey = "\033[90m";
    static constexpr std::string_view kComponent = "\033[1;36m";

    bool m_color = true;
    std::atomic<LogFormat> m_format;
    std::atomic<LogLevel> m_min_level{LogLevel::Info};

    static constexpr std::string_view Tag(const LogLevel level) noexcept {
        switch (level) {
            case LogLevel::Info:
                return "INFO";
            case LogLevel::Warning:
                return "WARN";
            case LogLevel::Error:
                return "ERROR";
            case LogLevel::Critical:
                return "CRIT";
        }
        return "?";
    }

    static constexpr std::string_view JsonLevel(const LogLevel level) noexcept {
        switch (level) {
            case LogLevel::Info:
                return "info";
            case LogLevel::Warning:
                return "warn";
            case LogLevel::Error:
                return "error";
            case LogLevel::Critical:
                return "critical";
        }
        return "unknown";
    }

    static constexpr std::string_view Color(const LogLevel level) noexcept {
        switch (level) {
            case LogLevel::Info:
                return "\033[32m";
            case LogLevel::Warning:
                return "\033[33m";
            case LogLevel::Error:
                return "\033[31m";
            case LogLevel::Critical:
                return "\033[1;97;41m";
        }
        return "";
    }

    static std::string Demangled(const char* mangled) {
        int status = 0;
        std::unique_ptr<char, decltype(&std::free)> name{
            abi::__cxa_demangle(mangled, nullptr, nullptr, &status), &std::free};
        return (status == 0 && name) ? std::string{name.get()} : std::string{mangled};
    }

    static std::size_t Utf8Length(const std::string_view s, const std::size_t i) noexcept {
        const auto byte = [&](const std::size_t k) { return static_cast<unsigned char>(s[k]); };
        const unsigned char c = byte(i);
        std::size_t length = 0;
        if (c >= 0xC2 && c <= 0xDF)
            length = 2;
        else if (c >= 0xE0 && c <= 0xEF)
            length = 3;
        else if (c >= 0xF0 && c <= 0xF4)
            length = 4;
        else
            return 0;

        if (i + length > s.size())
            return 0;
        for (std::size_t k = 1; k < length; ++k) {
            if ((byte(i + k) & 0xC0) != 0x80)
                return 0;
        }
        const unsigned char second = byte(i + 1);
        if (c == 0xE0 && second < 0xA0)
            return 0;
        if (c == 0xED && second >= 0xA0)
            return 0;
        if (c == 0xF0 && second < 0x90)
            return 0;
        if (c == 0xF4 && second >= 0x90)
            return 0;
        return length;
    }

    static void AppendJsonString(std::string& out, const std::string_view s) {
        static constexpr char kHex[] = "0123456789abcdef";
        out.push_back('"');
        for (std::size_t i = 0; i < s.size();) {
            const auto c = static_cast<unsigned char>(s[i]);
            if (c >= 0x80) {
                if (const std::size_t length = Utf8Length(s, i); length != 0) {
                    out.append(s.substr(i, length));
                    i += length;
                } else {
                    out.append("\\ufffd");
                    ++i;
                }
                continue;
            }
            switch (c) {
                case '"':
                    out.append("\\\"");
                    break;
                case '\\':
                    out.append("\\\\");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\r':
                    out.append("\\r");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                case '\b':
                    out.append("\\b");
                    break;
                case '\f':
                    out.append("\\f");
                    break;
                default:
                    if (c < 0x20) {
                        out.append("\\u00");
                        out.push_back(kHex[c >> 4]);
                        out.push_back(kHex[c & 0xF]);
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
            }
            ++i;
        }
        out.push_back('"');
    }

    static void AppendJsonValue(std::string& out, const LogField& f) {
        switch (f.kind) {
            case LogField::Kind::String:
                AppendJsonString(out, f.text);
                break;
            case LogField::Kind::Integer:
                out.append(std::to_string(f.integer));
                break;
            case LogField::Kind::Unsigned:
                out.append(std::to_string(f.unsigned_value));
                break;
            case LogField::Kind::Boolean:
                out.append(f.boolean ? "true" : "false");
                break;
        }
    }

    [[nodiscard]] std::string BuildJson(const LogLevel level, const std::string_view component,
                                        const std::string_view message,
                                        const std::initializer_list<LogField> fields,
                                        const std::chrono::sys_time<std::chrono::milliseconds> now,
                                        const unsigned long tid) const {
        std::string line;
        line.reserve(160 + message.size());
        line.append(R"({"ts":")");
        line.append(std::format("{:%FT%T}Z", now));
        line.append(R"(","level":")");
        line.append(JsonLevel(level));
        line.append(R"(","tid":)");
        line.append(std::to_string(tid));
        line.append(",\"component\":");
        AppendJsonString(line, component);
        line.append(",\"msg\":");
        AppendJsonString(line, message);
        for (const LogField& f : fields) {
            line.push_back(',');
            AppendJsonString(line, f.key);
            line.push_back(':');
            AppendJsonValue(line, f);
        }
        line.append("}\n");
        return line;
    }

    static void AppendTextValue(std::string& out, const LogField& f) {
        if (f.kind != LogField::Kind::String) {
            AppendJsonValue(out, f);
            return;
        }
        const bool plain =
            !f.text.empty() && f.text.find_first_of(" =\"\\\t\r\n") == std::string_view::npos;
        if (plain) {
            out.append(f.text);
        } else {
            AppendJsonString(out, f.text); // quoted and escaped, so a value never breaks the line
        }
    }

    [[nodiscard]] std::string BuildText(const LogLevel level, const std::string_view component,
                                        const std::string_view message,
                                        const std::initializer_list<LogField> fields,
                                        const std::chrono::sys_time<std::chrono::milliseconds> now,
                                        const unsigned long tid) const {
        std::string line =
            m_color
                ? std::format("{}{:%H:%M:%S}{} {}{:<5}{} {}t{:<5}{} {}{:<10}{} ", kGrey, now,
                              kReset, Color(level), Tag(level), kReset, kGrey, tid, kReset,
                              kComponent, component, kReset)
                : std::format("{:%H:%M:%S} {:<5} t{:<5} {:<10} ", now, Tag(level), tid, component);
        AppendTextMessage(line, message);
        for (const LogField& f : fields) {
            line.push_back(' ');
            line.append(f.key);
            line.push_back('=');
            AppendTextValue(line, f);
        }
        line.push_back('\n');
        return line;
    }

    static void AppendTextMessage(std::string& out, const std::string_view message) {
        for (const char c : message) {
            if (c == '\n')
                out.append("\\n");
            else if (c == '\r')
                out.append("\\r");
            else
                out.push_back(c);
        }
    }

  public:
    StderrLogger() noexcept
        : m_color(::isatty(STDERR_FILENO) != 0 && std::getenv("NO_COLOR") == nullptr)
        , m_format([] {
            const char* value = std::getenv("LOG_FORMAT");
            return (value != nullptr && std::strcmp(value, "json") == 0) ? LogFormat::Json
                                                                         : LogFormat::Text;
        }()) {}

    void SetFormat(const LogFormat format) noexcept {
        m_format.store(format, std::memory_order_relaxed);
    }
    void SetMinLevel(const LogLevel level) noexcept {
        m_min_level.store(level, std::memory_order_relaxed);
    }

    void Log(const LogLevel level, const std::string_view component, const std::string_view message,
             const std::initializer_list<LogField> fields = {}) const noexcept {
        if (level < m_min_level.load(std::memory_order_relaxed)) {
            return;
        }
        try {
            const auto now =
                std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
            const auto tid = static_cast<unsigned long>(::syscall(SYS_gettid));
            const std::string line = (m_format.load(std::memory_order_relaxed) == LogFormat::Json)
                                         ? BuildJson(level, component, message, fields, now, tid)
                                         : BuildText(level, component, message, fields, now, tid);
            (void)::write(STDERR_FILENO, line.data(), line.size());
        } catch (...) {
            [[maybe_unused]] int _;
        }
    }

    void LogInfo(std::string_view c, std::string_view m,
                 std::initializer_list<LogField> f = {}) const noexcept {
        Log(LogLevel::Info, c, m, f);
    }
    void LogWarning(std::string_view c, std::string_view m,
                    std::initializer_list<LogField> f = {}) const noexcept {
        Log(LogLevel::Warning, c, m, f);
    }
    void LogError(std::string_view c, std::string_view m,
                  std::initializer_list<LogField> f = {}) const noexcept {
        Log(LogLevel::Error, c, m, f);
    }
    void LogCritical(std::string_view c, std::string_view m,
                     std::initializer_list<LogField> f = {}) const noexcept {
        Log(LogLevel::Critical, c, m, f);
    }

    void LogException(const std::string_view component, const std::exception_ptr& error,
                      const LogLevel level = LogLevel::Error,
                      const std::initializer_list<LogField> extra = {}) const noexcept {
        try {
            std::string type = "unknown";
            std::string what;
            try {
                if (error) {
                    std::rethrow_exception(error);
                }
                what = "empty exception_ptr";
            } catch (const std::exception& e) {
                type = Demangled(typeid(e).name());
                what = e.what();
            } catch (...) {
                what = "not derived from std::exception";
            }

            if (m_format.load(std::memory_order_relaxed) == LogFormat::Json) {
                // the extra fields are appended by building the event by hand: initializer_lists
                // cannot be concatenated
                std::string message = "unhandled exception";
                const auto now =
                    std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
                const auto tid = static_cast<unsigned long>(::syscall(SYS_gettid));
                if (level < m_min_level.load(std::memory_order_relaxed)) {
                    return;
                }
                std::string line = BuildJson(level, component, message,
                                             {{"exception", type}, {"what", what}}, now, tid);
                if (extra.size() != 0) {
                    line.pop_back();
                    line.pop_back();
                    for (const LogField& f : extra) {
                        line.push_back(',');
                        AppendJsonString(line, f.key);
                        line.push_back(':');
                        AppendJsonValue(line, f);
                    }
                    line.append("}\n");
                }
                (void)::write(STDERR_FILENO, line.data(), line.size());
            } else {
                Log(level, component, std::format("{}: {}", type, what), extra);
            }
        } catch (...) {
            [[maybe_unused]] std::uint8_t _;
        }
    }
};

inline StderrLogger& GetLogger() noexcept {
    static StderrLogger logger;
    return logger;
}
} // namespace core_utility::stderr_logger
