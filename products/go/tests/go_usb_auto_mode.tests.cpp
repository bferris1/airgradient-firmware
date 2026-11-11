/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#include <catch2/catch_test_macros.hpp>

#include "go_usb_auto_mode.h"

namespace {

UsbAutoModeInputs ready(bool external, OperatingMode mode = OperatingMode::Portable) {
  UsbAutoModeInputs in{};
  in.enabled = true;
  in.external_power = external;
  in.mode = mode;
  in.tracking_active = false;
  in.has_saved_networks = true;
  in.can_switch = true;
  return in;
}

// Feed the same reading until the debounce accepts it; returns the last action.
UsbAutoModeAction settle(UsbAutoMode &m, const UsbAutoModeInputs &in) {
  UsbAutoModeAction action = UsbAutoModeAction::None;
  for (uint8_t i = 0; i < UsbAutoMode::USB_AUTO_MODE_DEBOUNCE_SAMPLES; ++i) {
    action = m.update(in);
  }
  return action;
}

} // namespace

TEST_CASE("plug-in switches Portable to Stationary after debounce", "[usb_auto_mode]") {
  UsbAutoMode m;
  REQUIRE(m.update(ready(false)) == UsbAutoModeAction::None);

  CHECK(m.update(ready(true)) == UsbAutoModeAction::None); // one sample is not enough
  CHECK(m.update(ready(true)) == UsbAutoModeAction::EnterStationary);
  CHECK(m.auto_active());

  // No repeat once Stationary.
  CHECK(m.update(ready(true, OperatingMode::Stationary)) == UsbAutoModeAction::None);
}

TEST_CASE("unplug returns to Portable only after an automatic switch", "[usb_auto_mode]") {
  UsbAutoMode m;
  m.update(ready(false));
  settle(m, ready(true));
  REQUIRE(m.auto_active());

  CHECK(m.update(ready(false, OperatingMode::Stationary)) == UsbAutoModeAction::None);
  CHECK(m.update(ready(false, OperatingMode::Stationary)) == UsbAutoModeAction::ReturnPortable);
  CHECK_FALSE(m.auto_active());
}

TEST_CASE("a one-sample power glitch does not change mode", "[usb_auto_mode]") {
  UsbAutoMode m;
  m.update(ready(false));
  settle(m, ready(true));
  REQUIRE(m.auto_active());

  CHECK(m.update(ready(false, OperatingMode::Stationary)) == UsbAutoModeAction::None);
  CHECK(m.update(ready(true, OperatingMode::Stationary)) == UsbAutoModeAction::None);
  CHECK(m.update(ready(false, OperatingMode::Stationary)) == UsbAutoModeAction::None);
  CHECK(m.auto_active());
}

TEST_CASE("manually chosen Stationary is left alone on unplug", "[usb_auto_mode]") {
  UsbAutoMode m;
  m.update(ready(true, OperatingMode::Stationary)); // boot plugged in, already Stationary
  CHECK_FALSE(m.auto_active());
  CHECK(settle(m, ready(false, OperatingMode::Stationary)) == UsbAutoModeAction::None);
}

TEST_CASE("manual mode change hands control back until the next plug-in", "[usb_auto_mode]") {
  UsbAutoMode m;
  m.update(ready(false));
  settle(m, ready(true));
  REQUIRE(m.auto_active());

  // User picks Portable while still plugged in: stay Portable.
  m.on_manual_mode_change();
  CHECK(settle(m, ready(true)) == UsbAutoModeAction::None);

  // Unplugging and plugging in again switches.
  settle(m, ready(false));
  CHECK(settle(m, ready(true)) == UsbAutoModeAction::EnterStationary);
}

TEST_CASE("plug-in while tracking waits for the route to stop", "[usb_auto_mode]") {
  UsbAutoMode m;
  m.update(ready(false));
  UsbAutoModeInputs tracking = ready(true);
  tracking.tracking_active = true;
  CHECK(settle(m, tracking) == UsbAutoModeAction::None);
  CHECK(m.update(tracking) == UsbAutoModeAction::None);

  CHECK(m.update(ready(true)) == UsbAutoModeAction::EnterStationary);
}

TEST_CASE("unplug before tracking stops cancels the pending switch", "[usb_auto_mode]") {
  UsbAutoMode m;
  m.update(ready(false));
  UsbAutoModeInputs tracking = ready(true);
  tracking.tracking_active = true;
  settle(m, tracking);
  tracking.external_power = false;
  settle(m, tracking);

  CHECK(m.update(ready(false)) == UsbAutoModeAction::None);
}

TEST_CASE("no switch without saved Wi-Fi or while another flow owns the screen",
          "[usb_auto_mode]") {
  UsbAutoMode m;
  m.update(ready(false));
  UsbAutoModeInputs in = ready(true);
  in.has_saved_networks = false;
  CHECK(settle(m, in) == UsbAutoModeAction::None);

  in.has_saved_networks = true;
  in.can_switch = false;
  CHECK(m.update(in) == UsbAutoModeAction::None);

  in.can_switch = true;
  CHECK(m.update(in) == UsbAutoModeAction::EnterStationary);
}

TEST_CASE("only Portable switches automatically", "[usb_auto_mode]") {
  UsbAutoMode m;
  m.update(ready(false, OperatingMode::Offline));
  CHECK(settle(m, ready(true, OperatingMode::Offline)) == UsbAutoModeAction::None);
  CHECK_FALSE(m.auto_active());
}

TEST_CASE("disabled setting never switches; enabling while plugged in does", "[usb_auto_mode]") {
  UsbAutoMode m;
  UsbAutoModeInputs off = ready(false);
  off.enabled = false;
  m.update(off);
  off.external_power = true;
  CHECK(settle(m, off) == UsbAutoModeAction::None);
  CHECK(m.update(off) == UsbAutoModeAction::None);

  CHECK(m.update(ready(true)) == UsbAutoModeAction::EnterStationary);
}

TEST_CASE("booting on USB counts as a plug-in", "[usb_auto_mode]") {
  UsbAutoMode m;
  CHECK(m.update(ready(true)) == UsbAutoModeAction::EnterStationary);
}

TEST_CASE("a failed switch is forgotten", "[usb_auto_mode]") {
  UsbAutoMode m;
  REQUIRE(m.update(ready(true)) == UsbAutoModeAction::EnterStationary);
  m.on_enter_failed();
  CHECK_FALSE(m.auto_active());
  CHECK(settle(m, ready(false)) == UsbAutoModeAction::None);
}

TEST_CASE("unreachable Wi-Fi is not retried until the next plug-in", "[usb_auto_mode]") {
  UsbAutoMode m;
  REQUIRE(m.update(ready(true)) == UsbAutoModeAction::EnterStationary);
  m.on_stationary_unavailable();
  CHECK_FALSE(m.auto_active());

  // Back in Portable, still plugged in: no retry.
  CHECK(settle(m, ready(true)) == UsbAutoModeAction::None);

  settle(m, ready(false));
  CHECK(settle(m, ready(true)) == UsbAutoModeAction::EnterStationary);
}
