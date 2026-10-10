#pragma once

#include <atomic>
#include <juce_core/juce_core.h>
#include <memory>

namespace mm::plugin {

enum class LogLevel { Error = 0, Warning = 1, Info = 2, Debug = 3 };

/// The log file of the plugin (SPEC 10): `Logs/midimaid.log` in the data folder, rotating (5 files of up to 1 MB). The
/// lines are written by a background thread, never by the caller (CLAUDE.md), and never from the audio thread.
///
/// What is logged: metadata (provider, model, duration, tokens, status code, schema error with path). **Never** an API
/// key. Prompts, answers and user content only through `writeContent`, and only when the musician switched on
/// "Prompts protokollieren" (default off, SPEC 10, D-86).
class AppLog {
public:
    static AppLog& instance();

    /// Where the lines go. An empty file switches the log off (the tests do that).
    void setFile(const juce::File& file);
    /// `<data folder>/Klirrwerk/MidiMaid/Logs/midimaid.log`, unless MIDIMAID_LOG_FILE names another file or says
    /// "none".
    static juce::File defaultFile();

    void setLevel(LogLevel level) { level_.store(static_cast<int>(level)); }
    LogLevel level() const { return static_cast<LogLevel>(level_.load()); }
    void setLogContent(bool on) { logContent_.store(on); }
    bool logContent() const { return logContent_.load(); }

    void write(LogLevel level, const juce::String& category, const juce::String& message);
    /// Prompts and answers: written only when "Prompts protokollieren" is on.
    void writeContent(const juce::String& category, const juce::String& label, const juce::String& text);
    /// Waits until everything that was handed over is on disk (tests, shutdown).
    void flush();

    static constexpr juce::int64 kMaxFileBytes = 1024 * 1024;
    static constexpr int kFiles = 5;

private:
    AppLog();
    ~AppLog();
    void append(const juce::String& line);

    juce::CriticalSection lock_; // the file name
    juce::File file_;
    std::atomic<int> level_{static_cast<int>(LogLevel::Info)};
    std::atomic<bool> logContent_{false};
    juce::ThreadPool pool_{1};
};

} // namespace mm::plugin
