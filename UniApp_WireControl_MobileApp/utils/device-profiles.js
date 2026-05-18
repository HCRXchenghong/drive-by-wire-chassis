import { DEFAULT_APP_CONFIG } from './chassis-protocol';

export const PROFILE_TEMPLATE = {
  id: '',
  name: 'F407 线控底盘',
  chassisCode: 'WC-F407-001',
  radioLink: 'ATK-BLE04',
  driveController: 'MSSD-60EHB_2D',
  steeringController: 'MSSC',
  controllerCore: 'STM32F407',
  connectionMode: 'BLE 透传',
  appRole: '手机 BLE Central',
  ewm22Role: 'BLE Peripheral + UART Bridge',
  bleNameExact: '',
  bleNamePrefix: 'infinite-robot-',
  bleNameKeyword: 'infinite-robot',
  bleNameHint: 'infinite-robot-001',
  frontTrackMm: DEFAULT_APP_CONFIG.frontTrackMm,
  wheelbaseMm: DEFAULT_APP_CONFIG.wheelbaseMm,
  rearTrackMm: DEFAULT_APP_CONFIG.rearTrackMm,
  driveMaxRpm: DEFAULT_APP_CONFIG.driveMaxRpm,
  steeringMaxRpm: DEFAULT_APP_CONFIG.steeringMaxRpm,
  steerCanNodeId: DEFAULT_APP_CONFIG.steerCanNodeId,
  handwheelCanNodeId: DEFAULT_APP_CONFIG.handwheelCanNodeId,
  chassisType: DEFAULT_APP_CONFIG.chassisType,
  driveAxle: DEFAULT_APP_CONFIG.driveAxle,
  hasLinearSteering: DEFAULT_APP_CONFIG.hasLinearSteering,
  pedalConfig: DEFAULT_APP_CONFIG.pedalConfig,
  status: 'ready',
  note: 'USART1 对接 ATK-BLE04 BLE 透传模块（广播名 infinite-robot-001~999），CAN1 驱动，CAN2 机械转向 + 线性方向盘。',
  lastConnectedAt: 0
};

export const DEVICE_PROFILES = [PROFILE_TEMPLATE];

/** Produce a deterministic profile id derived from the BLE broadcast name.
 *  Two pairings against the same module will yield the same id, which lets
 *  the storage layer dedupe by id instead of generating a fresh random
 *  UUID every time.  Non-word characters are folded to '-' so the id stays
 *  filesystem / URL friendly. */
export function bleNameToProfileId(bleName) {
  const safe = String(bleName || '')
    .trim()
    .replace(/[^0-9A-Za-z_.\-]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .toLowerCase();
  return safe ? `ble-${safe}` : `local-profile-${Date.now()}`;
}

/** Extract a short label (e.g. "001") from the tail of an
 *  "infinite-robot-XXX" BLE name so the UI can show it without the long
 *  prefix.  Falls back to the full name for other broadcasts. */
export function extractShortLabel(bleName) {
  const match = /infinite-robot-(\w+)$/i.exec(String(bleName || ''));
  return match ? match[1] : String(bleName || '');
}

export function createProfileFromBleDevice(deviceName) {
  const safeName = String(deviceName || 'BLE 设备').trim();
  const shortLabel = extractShortLabel(safeName);

  return {
    ...PROFILE_TEMPLATE,
    id: bleNameToProfileId(safeName),
    name: safeName,
    chassisCode: `WC-${shortLabel}`,
    bleNameExact: safeName,
    bleNameHint: safeName,
    lastConnectedAt: 0
  };
}
