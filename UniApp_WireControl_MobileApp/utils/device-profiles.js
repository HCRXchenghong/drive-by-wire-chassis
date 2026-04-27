import { DEFAULT_APP_CONFIG } from './chassis-protocol';

export const PROFILE_TEMPLATE = {
  id: '',
  name: 'F407 线控底盘',
  chassisCode: 'WC-F407-001',
  radioLink: 'EWM22A-xxxBWL22S',
  driveController: 'MSSD-60EHB_2D',
  steeringController: 'MSSC',
  controllerCore: 'STM32F407',
  connectionMode: 'BLE 透传',
  appRole: '手机 BLE Central',
  ewm22Role: 'BLE Peripheral + UART Bridge',
  bleNameExact: '',
  bleNamePrefix: 'EWM22A-',
  bleNameKeyword: 'EWM22',
  bleNameHint: 'EWM22A-xxxx',
  frontTrackMm: DEFAULT_APP_CONFIG.frontTrackMm,
  wheelbaseMm: DEFAULT_APP_CONFIG.wheelbaseMm,
  rearTrackMm: DEFAULT_APP_CONFIG.rearTrackMm,
  driveMaxRpm: DEFAULT_APP_CONFIG.driveMaxRpm,
  steerCanNodeId: DEFAULT_APP_CONFIG.steerCanNodeId,
  handwheelCanNodeId: DEFAULT_APP_CONFIG.handwheelCanNodeId,
  chassisType: DEFAULT_APP_CONFIG.chassisType,
  driveAxle: DEFAULT_APP_CONFIG.driveAxle,
  hasLinearSteering: DEFAULT_APP_CONFIG.hasLinearSteering,
  pedalConfig: DEFAULT_APP_CONFIG.pedalConfig,
  status: 'ready',
  note: 'USART1 对接 EWM22，CAN1 为驱动控制器，CAN2 为机械转向与线性方向盘两个 MSSC 节点共线。',
  lastConnectedAt: 0
};

export const DEVICE_PROFILES = [PROFILE_TEMPLATE];

function makeProfileId() {
  return `local-profile-${Date.now()}-${Math.floor(Math.random() * 10000)}`;
}

export function createProfileFromBleDevice(deviceName) {
  const safeName = String(deviceName || 'EWM22 设备');

  return {
    ...PROFILE_TEMPLATE,
    id: makeProfileId(),
    name: `${safeName} 底盘`,
    bleNameExact: safeName,
    bleNameHint: safeName,
    lastConnectedAt: 0
  };
}
