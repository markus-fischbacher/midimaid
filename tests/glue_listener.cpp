#include "NoUserSettings.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

class NoUserSettings : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        mm::plugin::overrideGlobalSettings(&mmtest::disabledSettings());
    }
    void testRunEnded(const Catch::TestRunStats&) override { mm::plugin::overrideGlobalSettings(nullptr); }
};

} // namespace

CATCH_REGISTER_LISTENER(NoUserSettings)
