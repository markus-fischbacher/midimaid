#include "plugin/MidiImportService.h"

namespace mm::plugin {

struct MidiImportService::State {
    Delivery delivery;
    bool alive = true;
    int pending = 0;
    uint64_t latest = 0;
};

MidiImportService::MidiImportService(Delivery delivery) : state_(std::make_shared<State>()) {
    state_->delivery = std::move(delivery);
}

MidiImportService::~MidiImportService() {
    cancel_->store(true);
    pool_.removeAllJobs(true, 10000);
    state_->alive = false;
}

bool MidiImportService::busy() const {
    return state_->pending > 0;
}

void MidiImportService::request(const ImportRequest& request) {
    cancel_->store(true); // the running read ends at its next step and delivers nothing
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    const uint64_t number = ++state_->latest;
    ++state_->pending;
    pool_.addJob([state = state_, cancel = cancel_, request, number] {
        ImportOutcome outcome;
        const auto size = request.file.getSize();
        if (!request.file.existsAsFile() || size <= 0) {
            outcome.unreadable = true;
        } else if (static_cast<uint64_t>(size) > mm::core::kMaxMidiFileBytes) {
            outcome.error = mm::core::MidiReadError::TooBig;
        } else {
            juce::MemoryBlock bytes;
            if (!request.file.loadFileAsData(bytes) || cancel->load()) {
                outcome.unreadable = !cancel->load();
            } else {
                const auto read = mm::core::readMidiFile(static_cast<const uint8_t*>(bytes.getData()), bytes.getSize());
                outcome.error = read.error;
                if (read.ok()) {
                    outcome.plan = mm::core::planImport(read.clip);
                }
            }
        }
        const bool cancelled = cancel->load();
        juce::MessageManager::callAsync([state, request, number, cancelled, outcome = std::move(outcome)]() mutable {
            if (!state->alive) {
                return;
            }
            --state->pending;
            if (cancelled || number != state->latest) {
                return;
            }
            state->delivery(request, std::move(outcome));
        });
    });
}

} // namespace mm::plugin
