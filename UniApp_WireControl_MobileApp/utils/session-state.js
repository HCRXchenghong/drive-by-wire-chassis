import { DEFAULT_APP_CONFIG, DEFAULT_CALIBRATION } from './chassis-protocol';
import { bleNameToProfileId } from './device-profiles';

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
  const cleanConfig = {};
  Object.keys(config || {}).forEach((key) => {
    const value = config[key];
    if (value !== undefined && value !== null) {
      cleanConfig[key] = value;
    }
  });

  sessionState.appConfig = {
    ...sessionState.appConfig,
    ...cleanConfig
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

/** Collapse the profile list so that each unique BLE name appears only
 *  once.  Older records whose ids predate deterministic name-based ids
 *  are migrated in place — we keep the entry with the most recent
 *  `lastConnectedAt` timestamp and drop the rest.  The canonical id is
 *  always derived from the BLE name, so future lookups by id will match
 *  freshly-created profiles too. */
function dedupeProfilesByBleName(profiles) {
  const byName = new Map();

  (profiles || []).forEach((profile) => {
    if (!profile) {
      return;
    }

    const bleName = String(profile.bleNameExact || profile.bleNameHint || '').trim();
    if (!bleName) {
      // Preserve entries that have no BLE name but still have an id so
      // users do not lose manually-authored profiles.
      if (profile.id) {
        byName.set(profile.id, profile);
      }
      return;
    }

    const key = bleName.toLowerCase();
    const canonicalId = bleNameToProfileId(bleName);
    const candidate = {
      ...profile,
      id: canonicalId
    };

    const previous = byName.get(key);
    if (!previous) {
      byName.set(key, candidate);
      return;
    }

    // Pick the freshest record and merge the other one's metadata.
    const prevAt = Number(previous.lastConnectedAt || 0);
    const nextAt = Number(candidate.lastConnectedAt || 0);
    const winner = nextAt >= prevAt ? candidate : previous;
    const loser = nextAt >= prevAt ? previous : candidate;

    byName.set(key, {
      ...loser,
      ...winner,
      id: canonicalId
    });
  });

  return Array.from(byName.values());
}

export function loadProfilesFromStorage() {
  try {
    const saved = uni.getStorageSync(LOCAL_PROFILE_STORAGE_KEY);
    const deduped = dedupeProfilesByBleName(Array.isArray(saved) ? saved : []);
    return sortProfiles(deduped);
  } catch (error) {
    return [];
  }
}

export function saveProfilesToStorage(profiles) {
  const deduped = dedupeProfilesByBleName(profiles || []);
  uni.setStorageSync(LOCAL_PROFILE_STORAGE_KEY, sortProfiles(deduped));
}

export function persistConnectedProfile(bridgeState) {
  if (!sessionState.connectedProfile) {
    return null;
  }

  const deviceName =
    (bridgeState && bridgeState.deviceName) ||
    sessionState.connectedProfile.bleNameExact ||
    sessionState.connectedProfile.bleNameHint;

  const canonicalId = bleNameToProfileId(deviceName);
  const nextProfile = {
    ...sessionState.connectedProfile,
    id: canonicalId,
    name: deviceName || sessionState.connectedProfile.name,
    bleNameExact: deviceName,
    bleNameHint: deviceName,
    frontTrackMm: sessionState.appConfig.frontTrackMm,
    wheelbaseMm: sessionState.appConfig.wheelbaseMm,
    rearTrackMm: sessionState.appConfig.rearTrackMm,
    driveMaxRpm: sessionState.appConfig.driveMaxRpm,
    steeringMaxRpm: sessionState.appConfig.steeringMaxRpm,
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
  const existedIndex = profiles.findIndex((item) => {
    if (!item) {
      return false;
    }

    if (item.id === nextProfile.id) {
      return true;
    }

    const savedName = String(item.bleNameExact || item.bleNameHint || '').toLowerCase();
    const incomingName = String(deviceName || '').toLowerCase();
    return !!savedName && savedName === incomingName;
  });

  if (existedIndex >= 0) {
    profiles.splice(existedIndex, 1, nextProfile);
  } else {
    profiles.unshift(nextProfile);
  }

  saveProfilesToStorage(profiles);
  sessionState.connectedProfile = clone(nextProfile);
  return nextProfile;
}
