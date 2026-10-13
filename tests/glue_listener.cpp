#include "NoUserSettings.h"
#include "plugin/EmbeddedTranslation.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

namespace {

class NoUserSettings : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo&) override {
        mm::plugin::overrideGlobalSettings(&mmtest::disabledSettings());
        mm::plugin::setUiLanguage("de"); // the texts of the tests do not depend on the computer
        mm::plugin::overrideReferenceStore(&mmtest::disabledReferences());
        mm::plugin::overrideUserGrooves(&mmtest::disabledUserGrooves());
    }
    void testRunEnded(const Catch::TestRunStats&) override {
        mm::plugin::overrideGlobalSettings(nullptr);
        mm::plugin::overrideReferenceStore(nullptr);
        mm::plugin::overrideUserGrooves(nullptr);
    }
};

} // namespace

CATCH_REGISTER_LISTENER(NoUserSettings)
