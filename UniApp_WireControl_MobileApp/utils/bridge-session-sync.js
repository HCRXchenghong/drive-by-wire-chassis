import {
  getSessionState,
  setCapabilities,
  setLinearSteeringState,
  updateAppConfig,
  updateCalibration
} from './session-state';

export function syncSessionFromBridgeState(bridgeState) {
  const state = bridgeState || {};

  if (state.configData) {
    updateAppConfig(state.configData);
  }

  if (state.chassisStatus) {
    updateAppConfig({
      chassisType: state.chassisStatus.chassisType,
      driveAxle: state.chassisStatus.driveAxle,
      steerCanNodeId: state.chassisStatus.steerCanNodeId,
      handwheelCanNodeId: state.chassisStatus.handwheelCanNodeId,
      leftDriveInverted: !!state.chassisStatus.leftDriveInverted,
      rightDriveInverted: !!state.chassisStatus.rightDriveInverted,
      hasLinearSteering: !!state.chassisStatus.linearSteeringEnabled
    });

    setLinearSteeringState({
      enabled: !!state.chassisStatus.linearSteeringEnabled,
      detected: !!state.chassisStatus.linearSteeringDetected,
      steerCanReady: !!state.chassisStatus.steerCanReady
    });
  }

  if (state.statusData) {
    updateAppConfig({
      steerCanNodeId: state.statusData.steerCanNodeId,
      handwheelCanNodeId: state.statusData.handwheelCanNodeId,
      leftDriveInverted: !!state.statusData.leftDriveInverted,
      rightDriveInverted: !!state.statusData.rightDriveInverted,
      hasLinearSteering: !!state.statusData.linearSteeringEnabled
    });

    setLinearSteeringState({
      enabled: !!state.statusData.linearSteeringEnabled,
      detected: !!state.statusData.linearSteeringDetected,
      steerCanReady: !!state.statusData.steerCanReady
    });
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
