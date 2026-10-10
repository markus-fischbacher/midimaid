#include "engine/PlayheadMeter.h"

#include <atomic>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <thread>

using mm::engine::PlayheadMeter;

TEST_CASE("a meter that was never written has no position", "[playhead-meter]") {
    PlayheadMeter meter;
    CHECK_FALSE(meter.ppqAt(0.0).has_value());
    CHECK_FALSE(meter.ppqAt(1000.0).has_value());
}

TEST_CASE("the position runs forward with the tempo from the block start", "[playhead-meter]") {
    PlayheadMeter meter;
    meter.publish(true, 8.0, 120.0, 100.0);
    CHECK(*meter.ppqAt(100.0) == Catch::Approx(8.0));
    CHECK(*meter.ppqAt(100.1) == Catch::Approx(8.2)); // 120 BPM: two beats a second
    CHECK(*meter.ppqAt(100.25) == Catch::Approx(8.5));
    meter.publish(true, 8.0, 60.0, 100.0);
    CHECK(*meter.ppqAt(100.2) == Catch::Approx(8.2)); // 60 BPM: one beat a second
    // a clock a little behind the block does not run the position backwards
    CHECK(*meter.ppqAt(99.9) == Catch::Approx(8.0));
}

TEST_CASE("an old reading, a stopped transport and a negative position give nothing", "[playhead-meter]") {
    PlayheadMeter meter;
    meter.publish(true, 8.0, 120.0, 100.0);
    CHECK(meter.ppqAt(100.26).has_value() == false); // no block for longer than kMaxAgeSeconds
    CHECK(meter.ppqAt(100.24).has_value());
    meter.publish(false, 8.0, 120.0, 100.0);
    CHECK_FALSE(meter.ppqAt(100.0).has_value());
    meter.publish(true, -1.5, 120.0, 100.0);
    CHECK_FALSE(meter.ppqAt(100.0).has_value());
    meter.publish(true, 3.0, 0.0, 100.0); // no tempo
    CHECK_FALSE(meter.ppqAt(100.0).has_value());
    meter.publish(true, 3.0, 120.0, 100.0); // the next block brings it back
    CHECK(meter.ppqAt(100.0).has_value());
}

TEST_CASE("a reader never sees a reading that is half old and half new", "[playhead-meter]") {
    PlayheadMeter meter;
    std::atomic<bool> stop{false};
    // `playing` is true exactly for even positions, the tempo is fixed and the clock stamp is the same for all: a
    // reading made of two writes would show an odd position or a missing one for an even number.
    std::thread writer([&] {
        for (int n = 0; !stop.load(); ++n) {
            meter.publish(n % 2 == 0, static_cast<double>(n % 1000), 60.0, 50.0);
        }
    });
    int seen = 0;
    int torn = 0;
    for (int i = 0; i < 200000; ++i) {
        if (const auto value = meter.ppqAt(50.0)) {
            ++seen;
            const double whole = std::round(*value);
            if (std::abs(*value - whole) > 1e-9 || static_cast<long long>(whole) % 2 != 0) {
                ++torn;
            }
        }
    }
    stop = true;
    writer.join();
    CHECK(seen > 0);
    CHECK(torn == 0);
}
