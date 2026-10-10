#include "plugin/AppLog.h"

namespace mm::plugin {

namespace {

const char* nameOf(LogLevel level) {
    switch (level) {
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Warning:
        return "WARN ";
    case LogLevel::Info:
        return "INFO ";
    case LogLevel::Debug:
        return "DEBUG";
    }
    return "INFO ";
}

/// A log line is one line: a new line inside the text would look like a new entry.
juce::String oneLine(juce::String text) {
    return text.replaceCharacters("\r\n\t", "   ");
}

} // namespace

AppLog::AppLog() = default;

AppLog::~AppLog() {
    pool_.removeAllJobs(false, 3000);
}

AppLog& AppLog::instance() {
    static AppLog log;
    return log;
}

juce::File AppLog::defaultFile() {
    const auto fromEnvironment = juce::SystemStats::getEnvironmentVariable("MIDIMAID_LOG_FILE", {});
    if (fromEnvironment == "none") {
        return {};
    }
    if (fromEnvironment.isNotEmpty()) {
        return juce::File(fromEnvironment);
    }
    auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    if ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0) {
        folder = folder.getChildFile("Application Support");
    }
    return folder.getChildFile("Klirrwerk").getChildFile("MidiMaid").getChildFile("Logs").getChildFile("midimaid.log");
}

void AppLog::setFile(const juce::File& file) {
    const juce::ScopedLock lock(lock_);
    file_ = file;
}

void AppLog::write(LogLevel level, const juce::String& category, const juce::String& message) {
    if (static_cast<int>(level) > level_.load()) {
        return;
    }
    juce::File target;
    {
        const juce::ScopedLock lock(lock_);
        target = file_;
    }
    if (target == juce::File()) {
        return;
    }
    const auto line = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H:%M:%S") + " " + nameOf(level) + " [" +
                      category + "] " + oneLine(message) + "\n";
    pool_.addJob([this, target, line] {
        const juce::ScopedLock lock(lock_);
        if (target != file_) {
            return; // the file changed meanwhile
        }
        append(line);
    });
}

void AppLog::writeContent(const juce::String& category, const juce::String& label, const juce::String& text) {
    if (!logContent_.load()) {
        return;
    }
    write(LogLevel::Info, category, label + ": " + text);
}

void AppLog::append(const juce::String& line) {
    if (!file_.getParentDirectory().createDirectory()) {
        return;
    }
    if (file_.getSize() >= kMaxFileBytes) {
        // midimaid.log.4 is dropped, .3 becomes .4 and so on, the log becomes .1
        file_.getSiblingFile(file_.getFileName() + "." + juce::String(kFiles - 1)).deleteFile();
        for (int i = kFiles - 2; i >= 1; --i) {
            const auto older = file_.getSiblingFile(file_.getFileName() + "." + juce::String(i));
            if (older.existsAsFile()) {
                older.moveFileTo(file_.getSiblingFile(file_.getFileName() + "." + juce::String(i + 1)));
            }
        }
        file_.moveFileTo(file_.getSiblingFile(file_.getFileName() + ".1"));
    }
    file_.appendText(line, false, false, "\n");
}

void AppLog::flush() {
    while (pool_.getNumJobs() > 0) {
        juce::Thread::sleep(2);
    }
}

} // namespace mm::plugin
