#include "core/SlotBank.h"

#include "core/PatternValidation.h"

#include <algorithm>
#include <utility>

namespace mm::core {

std::string sanitizeSlotName(std::string_view name) {
    std::string cleaned;
    cleaned.reserve(name.size());
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte != 0x7F) {
            cleaned.push_back(c);
        }
    }
    const auto first = cleaned.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    cleaned = cleaned.substr(first, cleaned.find_last_not_of(' ') - first + 1);
    if (cleaned.size() > kMaxNameBytes) {
        size_t end = kMaxNameBytes;
        // Do not cut inside a multi-byte character (continuation bytes are 10xxxxxx).
        while (end > 0 && (static_cast<unsigned char>(cleaned[end]) & 0xC0) == 0x80) {
            --end;
        }
        cleaned.resize(end);
        const auto last = cleaned.find_last_not_of(' ');
        cleaned.resize(last == std::string::npos ? 0 : last + 1);
    }
    return cleaned;
}

const Slot* SlotBank::slot(size_t index) const {
    return valid(index) ? &slots_[index] : nullptr;
}

bool SlotBank::isEmpty(size_t index) const {
    return !valid(index) || !slots_[index].pattern.has_value();
}

bool SlotBank::store(size_t index, Pattern pattern, bool asResult) {
    if (!valid(index) || !validatePattern(pattern).empty()) {
        return false;
    }
    Slot& slot = slots_[index];
    pattern.version = nextVersion_++;
    if (asResult) {
        slot.history.push_back(pattern);
        if (slot.history.size() > kMaxHistory) {
            slot.history.erase(slot.history.begin());
        }
        slot.cursor = slot.history.size() - 1;
    }
    slot.pattern = std::move(pattern);
    ++slot.revision;
    return true;
}

bool SlotBank::setResult(size_t index, Pattern pattern) {
    return store(index, std::move(pattern), true);
}

bool SlotBank::edit(size_t index, Pattern pattern) {
    return valid(index) && slots_[index].pattern.has_value() && store(index, std::move(pattern), false);
}

bool SlotBank::historyBack(size_t index) {
    if (!valid(index) || slots_[index].history.empty() || slots_[index].cursor == 0) {
        return false;
    }
    Slot& slot = slots_[index];
    --slot.cursor;
    slot.pattern = slot.history[slot.cursor];
    ++slot.revision;
    return true;
}

bool SlotBank::historyForward(size_t index) {
    if (!valid(index) || slots_[index].history.empty() || slots_[index].cursor + 1 >= slots_[index].history.size()) {
        return false;
    }
    Slot& slot = slots_[index];
    ++slot.cursor;
    slot.pattern = slot.history[slot.cursor];
    ++slot.revision;
    return true;
}

bool SlotBank::setName(size_t index, std::string_view name) {
    if (!valid(index)) {
        return false;
    }
    slots_[index].name = sanitizeSlotName(name);
    ++slots_[index].revision;
    return true;
}

bool SlotBank::setColor(size_t index, uint8_t color) {
    if (!valid(index) || color > kMaxColor) {
        return false;
    }
    slots_[index].color = color;
    ++slots_[index].revision;
    return true;
}

std::optional<SlotContent> SlotBank::copy(size_t index) const {
    if (!valid(index)) {
        return std::nullopt;
    }
    const Slot& slot = slots_[index];
    return SlotContent{slot.pattern, slot.name, slot.color};
}

bool SlotBank::paste(size_t index, const SlotContent& content) {
    if (!valid(index) || !content.pattern.has_value() || content.color > kMaxColor ||
        !store(index, *content.pattern, true)) {
        return false;
    }
    slots_[index].name = sanitizeSlotName(content.name);
    slots_[index].color = content.color;
    return true;
}

bool SlotBank::clear(size_t index) {
    if (!valid(index)) {
        return false;
    }
    Slot& slot = slots_[index];
    const uint64_t revision = slot.revision + 1;
    slot = Slot{};
    slot.revision = revision;
    return true;
}

bool SlotBank::swap(size_t first, size_t second) {
    if (!valid(first) || !valid(second)) {
        return false;
    }
    if (first != second) {
        Slot& a = slots_[first];
        Slot& b = slots_[second];
        std::swap(a.pattern, b.pattern);
        std::swap(a.name, b.name);
        std::swap(a.color, b.color);
        std::swap(a.history, b.history);
        std::swap(a.cursor, b.cursor);
        ++a.revision;
        ++b.revision;
    }
    return true;
}

void SlotBank::restore(size_t index, Slot slot) {
    if (valid(index)) {
        slot.revision = slots_[index].revision + 1;
        slots_[index] = std::move(slot);
    }
}

} // namespace mm::core
