import { DEFAULT_APP_CONFIG, DEFAULT_CALIBRATION } from './chassis-protocol';

export const LOCAL_PROFILE_STORAGE_KEY = 'wire_control_local_profiles_v2';

const sessionState = {
  pendingProfile: null,
  connectedProfile: null,
  appConfig: { ...DEFAULT_APP_CONFIG },
  calibration: { ...DEFAULT_CALIBRATION },
  capabilities: null,
  linearSteeringState: {
    enabled: false,
    detected: false,
    protocolSupported: false,
    steerCanReady: false,
    steerCanRxCount: 0,
    steerCanLastRxMs: 0,
    reason: ''
  }
};

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function sortProfiles(profiles) {
  return (profiles || []).slice().sort((left, right) => (right.lastConnectedAt || 0) - (left.lastConnectedAt || 0));
}

export function getSessionState() {
  return clone(sessionState);
}

export function setPendingProfile(profile) {
  sessionState.pendingProfile = profile ? clone(profile) : null;
}

export function getPendingProfile() {
  return sessionState.pendingProfile ? clone(sessionState.pendingProfile) : null;
}

export function setConnectedProfile(profile) {
  sessionState.connectedProfile = profile ? clone(profile) : null;
}

export function getConnectedProfile() {
  return sessionState.connectedProfile ? clone(sessionState.connectedProfile) : null;
}

export function updateAppConfig(config) {
  sessionState.appConfig = {
    ...sessionState.appConfig,
    ...(config || {})
  };
}

export function getAppConfig() {
  return clone(sessionState.appConfig);
}

export function updateCalibration(calibration) {
  sessionState.calibration = {
    ...sessionState.calibration,
    ...(calibration || {})
  };
}

export function getCalibration() {
  return clone(sessionState.calibration);
}

export function setCapabilities(capabilities) {
  sessionState.capabilities = capabilities ? clone(capabilities) : null;
}

export function getCapabilities() {
  return sessionState.capabilities ? clone(sessionState.capabilities) : null;
}

export function setLinearSteeringState(state) {
  sessionState.linearSteeringState = {
    ...sessionState.linearSteeringState,
    ...(state || {})
  };
}

export function getLinearSteeringState() {
  return clone(sessionState.linearSteeringState);
}

export function resetSessionState() {
  sessionState.pendingProfile = null;
  sessionState.connectedProfile = null;
  sessionState.appConfig = { ...DEFAULT_APP_CONFIG };
  sessionState.calibration = { ...DEFAULT_CALIBRATION };
  sessionState.capabilities = null;
  sessionState.linearSteeringState = {
    enabled: false,
    detected: false,
    protocolSupported: false,
    steerCanReady: false,
    steerCanRxCount: 0,
    steerCanLastRxMs: 0,
    reason: ''
  };
}

export function loadProfilesFromStorage() {
  try {
    const saved = uni.getStorageSync(LOCAL_PROFILE_STORAGE_KEY);
    return Array.isArray(saved) ? sortProfiles(saved) : [];
  } catch (error) {
    return [];
  }
}

export function saveProfilesToStorage(profiles) {
  uni.setStorageSync(LOCAL_PROFILE_STORAGE_KEY, sortProfiles(profiles));
}

export function persistConnectedProfile(bridgeState) {
  if (!sessionState.connectedProfile) {
    return null;
  }

  const deviceName =
    (bridgeState && bridgeState.deviceName) ||
    sessionState.connectedProfile.bleNameExact ||
    sessionState.connectedProfile.bleNameHint;

  const nextProfile = {
    ...sessionState.connectedProfile,
    bleNameExact: deviceName,
    bleNameHint: deviceName,
    frontTrackMm: sessionState.appConfig.frontTrackMm,
    wheelbaseMm: sessionState.appConfig.wheelbaseMm,
    rearTrackMm: sessionState.appConfig.rearTrackMm,
    driveMaxRpm: sessionState.appConfig.driveMaxRpm,
    steerCanNodeId: sessionState.appConfig.steerCanNodeId,
    handwheelCanNodeId: sessionState.appConfig.handwheelCanNodeId,
    leftDriveInverted: !!sessionState.appConfig.leftDriveInverted,
    rightDriveInverted: !!sessionState.appConfig.rightDriveInverted,
    chassisType: sessionState.appConfig.chassisType,
    driveAxle: sessionState.appConfig.driveAxle,
    hasLinearSteering: sessionState.appConfig.hasLinearSteering,
    pedalConfig: sessionState.appConfig.pedalConfig,
    lastConnectedAt: Date.now()
  };

  const profiles = loadProfilesFromStorage();
  const existedIndex = profiles.findIndex((item) => item.id === nextProfile.id);

  if (existedIndex >= 0) {
    profiles.splice(existedIndex, 1, nextProfile);
  } else {
    profiles.unshift(nextProfile);
  }

  saveProfilesToStorage(profiles);
  sessionState.connectedProfile = clone(nextProfile);
  return nextProfile;
}
