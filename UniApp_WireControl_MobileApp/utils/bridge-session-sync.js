import {
  getSessionState,
  setCapabilities,
  setLinearSteeringState,
  updateAppConfig,
  updateCalibration
} from './session-state';

export function syncSessionFromBridgeState(bridgeState) {
  const state = bridgeState || {};
  const setKnownLinearState = (source) => {
    const patch = {};
    [
      ['enabled', source && source.linearSteeringEnabled],
      ['detected', source && source.linearSteeringDetected],
      ['steerCanReady', source && source.steerCanReady]
    ].forEach(([key, value]) => {
      if (value !== undefined && value !== null) {
        patch[key] = !!value;
      }
    });

    if (Object.keys(patch).length > 0) {
      setLinearSteeringState(patch);
    }
  };

  if (state.configData) {
    updateAppConfig(state.configData);
  }

  if (state.chassisStatus) {
    setKnownLinearState(state.chassisStatus);
  }

  if (state.statusData) {
    const statusConfigPatch = {};
    [
      ['driveMaxRpm', state.statusData.driveMaxRpm],
      ['steeringMaxRpm', state.statusData.steeringMaxRpm],
      ['steerCanNodeId', state.statusData.steerCanNodeId],
      ['handwheelCanNodeId', state.statusData.handwheelCanNodeId],
      ['leftDriveInverted', state.statusData.leftDriveInverted],
      ['rightDriveInverted', state.statusData.rightDriveInverted],
      ['hasLinearSteering', state.statusData.linearSteeringEnabled]
    ].forEach(([key, value]) => {
      if (value !== undefined && value !== null) {
        statusConfigPatch[key] = value;
      }
    });
    updateAppConfig(statusConfigPatch);

    setKnownLinearState(state.statusData);
  }

  if (state.capabilities) {
    setCapabilities(state.capabilities);
    setLinearSteeringState({
      protocolSupported: !!state.capabilities.linearSteeringSupported
    });
  }

  if (state.calibrationData) {
    updateCalibration(state.calibrationData);
  }

  if (state.linearSteering) {
    setLinearSteeringState(state.linearSteering);
  }

  return getSessionState();
}
