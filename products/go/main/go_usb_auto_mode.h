/**
 * AirGradient
 * https://airgradient.com
 *
 * CC BY-SA 4.0 Attribution-ShareAlike 4.0 International License
 */

#ifndef GO_USB_AUTO_MODE_H
#define GO_USB_AUTO_MODE_H

#include <cstdint>

#include "go_types.h"

/// Snapshot of everything the USB auto-mode decision depends on, sampled on
/// each charger status poll.
struct UsbAutoModeInputs {
  bool enabled = false;          ///< GoSettings::auto_mode_on_usb
  bool external_power = false;   ///< Raw charger reading: USB / adapter present
  OperatingMode mode = OperatingMode::Portable;
  bool tracking_active = false;  ///< Recording or paused route
  bool has_saved_networks = false;
  bool can_switch = false;       ///< No onboarding, setup session or manufacturing
};

enum class UsbAutoModeAction : uint8_t {
  None,
  EnterStationary, ///< Switch Portable -> Stationary without persisting it
  ReturnPortable,  ///< Undo an earlier EnterStationary
};

/// Decides when plugging in or unplugging USB should change the operating
/// mode. Pure logic: the orchestrator feeds it charger samples and applies the
/// returned action.
///
/// - Power changes are debounced over USB_AUTO_MODE_DEBOUNCE_SAMPLES samples,
///   so a brief rail drop on unplug does not flip the mode.
/// - A plug-in (or boot, or enabling the setting, while plugged in) arms one
///   switch to Stationary. The switch waits while a route is tracked, when no
///   Wi-Fi network is saved, or while another flow owns the screen, and fires
///   once those clear if USB is still present.
/// - Unplugging returns to Portable only when this logic entered Stationary.
///   A manual mode change hands control back to the user until the next
///   plug-in.
///
/// ISR-safe: no
/// Thread-safe: no (orchestrator task only)
/// Blocking: no
class UsbAutoMode {
public:
  /// Consecutive charger samples (5 s apart) needed to accept a power change.
  static constexpr uint8_t USB_AUTO_MODE_DEBOUNCE_SAMPLES = 2;

  UsbAutoModeAction update(const UsbAutoModeInputs &in) {
    const PowerEdge edge = debounce(in.external_power);

    if (edge == PowerEdge::Removed) {
      _armed = false;
      if (_auto_active) {
        _auto_active = false;
        if (in.mode == OperatingMode::Stationary) {
          return UsbAutoModeAction::ReturnPortable;
        }
      }
      _prev_enabled = in.enabled;
      return UsbAutoModeAction::None;
    }

    if (edge == PowerEdge::Inserted || (in.enabled && !_prev_enabled && _stable_external)) {
      _armed = true;
    }
    _prev_enabled = in.enabled;

    if (in.enabled && _armed && _stable_external && in.mode == OperatingMode::Portable &&
        !in.tracking_active && in.has_saved_networks && in.can_switch) {
      _armed = false;
      _auto_active = true;
      return UsbAutoModeAction::EnterStationary;
    }
    return UsbAutoModeAction::None;
  }

  /// The user (or a remote config) changed the mode: stop managing it until
  /// the next plug-in.
  void on_manual_mode_change() {
    _auto_active = false;
    _armed = false;
  }

  /// The automatic Stationary could not reach Wi-Fi. The caller returns to
  /// Portable; do not retry until the next plug-in.
  void on_stationary_unavailable() {
    _auto_active = false;
    _armed = false;
  }

  /// The orchestrator could not apply EnterStationary; forget the switch.
  void on_enter_failed() { _auto_active = false; }

  /// True while the current Stationary mode was entered by this logic.
  bool auto_active() const { return _auto_active; }

private:
  enum class PowerEdge : uint8_t { None, Inserted, Removed };

  PowerEdge debounce(bool external) {
    if (!_initialized) {
      // The first sample is trusted: booting on USB counts as a plug-in.
      _initialized = true;
      _stable_external = external;
      return external ? PowerEdge::Inserted : PowerEdge::None;
    }
    if (external == _stable_external) {
      _pending_samples = 0;
      return PowerEdge::None;
    }
    if (++_pending_samples < USB_AUTO_MODE_DEBOUNCE_SAMPLES) {
      return PowerEdge::None;
    }
    _pending_samples = 0;
    _stable_external = external;
    return external ? PowerEdge::Inserted : PowerEdge::Removed;
  }

  bool _initialized = false;
  bool _stable_external = false;
  uint8_t _pending_samples = 0;
  bool _prev_enabled = false;
  bool _armed = false;
  bool _auto_active = false;
};

#endif // GO_USB_AUTO_MODE_H
