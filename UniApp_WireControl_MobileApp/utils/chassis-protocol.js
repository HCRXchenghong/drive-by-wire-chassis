export const REMOTE_BUTTONS = {
  ESTOP: 0x0001,
  SOFT_STOP: 0x0002
};

export const DEFAULT_APP_CONFIG = {
  chassisType: 'ackermann',
  driveAxle: 'rwd',
  frontTrackMm: 1280,
  wheelbaseMm: 1720,
  rearTrackMm: 1260,
  steerCanNodeId: 1,
  handwheelCanNodeId: 2,
  leftDriveInverted: false,
  rightDriveInverted: false,
  hasLinearSteering: false,
  pedalConfig: 'brake_throttle'
};

export const DEFAULT_CALIBRATION = {
  throttleRawMin: 240,
  throttleRawMax: 3840,
  brakeRawMin: 240,
  brakeRawMax: 3840,
  steeringCenter: 0,
  steeringLeft10: -100,
  steeringRight10: 100,
  steeringLeftLimit: -600,
  steeringRightLimit: 600,
  handwheelCenter: 0,
  handwheelLeft10: -100,
  handwheelRight10: 100,
  handwheelLeftLimit: -600,
  handwheelRightLimit: 600,
  steeringEstimate: 0,
  handwheelEstimate: 0
};

function clamp(value, minValue, maxValue) {
  if (value < minValue) {
    return minValue;
  }

  if (value > maxValue) {
    return maxValue;
  }

  return value;
}

function percentToAxis(percentValue) {
  return clamp(Math.round(Number(percentValue || 0) * 10), -1000, 1000);
}

function safeNumber(value, fallback) {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : fallback;
}

function safeBool(value, fallback) {
  if (typeof value === 'boolean') {
    return value;
  }

  if (typeof value === 'number') {
    return value !== 0;
  }

  if (typeof value === 'string') {
    const normalized = value.trim().toUpperCase();
    if (normalized === 'TRUE' || normalized === '1' || normalized === 'YES' || normalized === 'ON') {
      return true;
    }

    if (normalized === 'FALSE' || normalized === '0' || normalized === 'NO' || normalized === 'OFF') {
      return false;
    }
  }

  return !!fallback;
}

export function createControlFrame(options) {
  const safe = options || {};

  return {
    cmd: 'app_control',
    seq: safe.seq || 1,
    gear: String(safe.gear || 'N').toUpperCase(),
    throttle: percentToAxis(safe.throttlePercent || 0),
    steer: percentToAxis(safe.steerPercent || 0),
    aux_x: percentToAxis(safe.auxXPercent || 0),
    aux_y: percentToAxis(safe.auxYPercent || 0),
    buttons: safe.buttons || 0
  };
}

export function serializeControlFrame(frame) {
  return `${JSON.stringify(frame)}\n`;
}

export function serializeCommand(command) {
  return `${JSON.stringify(command || {})}\n`;
}

export function createNeutralFrame(seq, buttons, gear) {
  return createControlFrame({
    seq,
    gear: gear || 'N',
    throttlePercent: 0,
    steerPercent: 0,
    auxXPercent: 0,
    auxYPercent: 0,
    buttons: buttons || 0
  });
}

export function createGetConfigCommand() {
  return { cmd: 'get_app_config' };
}

export function createSetConfigCommand(config) {
  const safe = {
    ...DEFAULT_APP_CONFIG,
    ...(config || {})
  };

  return {
    cmd: 'set_app_config',
    chassis_type: String(safe.chassisType || 'ackermann').toUpperCase(),
    drive_axle: String(safe.driveAxle || 'rwd').toUpperCase(),
    front_track_mm: Math.max(100, Math.round(safeNumber(safe.frontTrackMm, DEFAULT_APP_CONFIG.frontTrackMm))),
    wheelbase_mm: Math.max(100, Math.round(safeNumber(safe.wheelbaseMm, DEFAULT_APP_CONFIG.wheelbaseMm))),
    rear_track_mm: Math.max(100, Math.round(safeNumber(safe.rearTrackMm, DEFAULT_APP_CONFIG.rearTrackMm))),
    steer_can_node_id: clamp(Math.round(safeNumber(safe.steerCanNodeId, DEFAULT_APP_CONFIG.steerCanNodeId)), 1, 0x7FF),
    handwheel_can_node_id: clamp(Math.round(safeNumber(safe.handwheelCanNodeId, DEFAULT_APP_CONFIG.handwheelCanNodeId)), 1, 0x7FF),
    left_drive_inverted: !!safe.leftDriveInverted,
    right_drive_inverted: !!safe.rightDriveInverted,
    linear_steering_enabled: !!safe.hasLinearSteering,
    pedal_config: String(safe.pedalConfig || DEFAULT_APP_CONFIG.pedalConfig).toUpperCase()
  };
}

export function createSaveConfigCommand() {
  return { cmd: 'save_app_config' };
}

export function createLoadConfigCommand() {
  return { cmd: 'load_app_config' };
}

export function createGetStatusCommand() {
  return { cmd: 'get_status' };
}

export function createGetCapabilitiesCommand() {
  return { cmd: 'get_capabilities' };
}

export function createQueryLinearSteeringCommand() {
  return { cmd: 'query_linear_steering' };
}

export function createGetCalibrationCommand() {
  return { cmd: 'get_calibration' };
}

export function createSetRemoteModeCommand(mode) {
  return {
    cmd: 'set_remote_mode',
    mode: String(mode || 'MONITOR').toUpperCase()
  };
}

export function createSetRemoteStopStateCommand(options) {
  const safe = options || {};

  return {
    cmd: 'set_remote_stop_state',
    soft_stop: !!safe.softStop,
    estop: !!safe.estop
  };
}

export function createSteeringJogCommand(axis, rpm) {
  return {
    cmd: 'steering_jog',
    axis: String(axis || 'steering').toUpperCase(),
    rpm: Math.round(safeNumber(rpm, 0))
  };
}

export function createCalibrationPointCommand(axis, point) {
  return {
    cmd: 'set_calibration_point',
    axis: String(axis || 'steering').toUpperCase(),
    point: String(point || 'center').toUpperCase()
  };
}

export function createPedalSampleCommand(pedal, phase, options) {
  const command = {
    cmd: 'capture_pedal_sample',
    pedal: String(pedal || 'left').toUpperCase(),
    phase: String(phase || 'released').toUpperCase()
  };

  if (options && options.reset) {
    command.reset = true;
  }

  return command;
}

export function createSaveCalibrationCommand() {
  return { cmd: 'save_calibration' };
}

export function normalizeConfigPayload(payload) {
  const source = payload || {};

  return {
    chassisType: String(source.chassis_type || source.ct || DEFAULT_APP_CONFIG.chassisType).toLowerCase() === 'diff' ? 'diff' : 'ackermann',
    driveAxle: String(source.drive_axle || source.da || DEFAULT_APP_CONFIG.driveAxle).toLowerCase() === 'fwd' ? 'fwd' : 'rwd',
    frontTrackMm: Math.round(safeNumber(source.front_track_mm || source.ft, DEFAULT_APP_CONFIG.frontTrackMm)),
    wheelbaseMm: Math.round(safeNumber(source.wheelbase_mm || source.wb, DEFAULT_APP_CONFIG.wheelbaseMm)),
    rearTrackMm: Math.round(safeNumber(source.rear_track_mm || source.rt, DEFAULT_APP_CONFIG.rearTrackMm)),
    steerCanNodeId: Math.round(safeNumber(source.steer_can_node_id || source.sid, DEFAULT_APP_CONFIG.steerCanNodeId)),
    handwheelCanNodeId: Math.round(safeNumber(source.handwheel_can_node_id || source.hid, DEFAULT_APP_CONFIG.handwheelCanNodeId)),
    leftDriveInverted: safeBool(source.left_drive_inverted || source.ldi, DEFAULT_APP_CONFIG.leftDriveInverted),
    rightDriveInverted: safeBool(source.right_drive_inverted || source.rdi, DEFAULT_APP_CONFIG.rightDriveInverted),
    hasLinearSteering: safeBool(source.linear_steering_enabled || source.ls, DEFAULT_APP_CONFIG.hasLinearSteering),
    pedalConfig: String(source.pedal_config || DEFAULT_APP_CONFIG.pedalConfig).toLowerCase() === 'estop_throttle'
      ? 'estop_throttle'
      : 'brake_throttle'
  };
}

export function normalizeCalibrationPayload(payload) {
  const source = payload || {};

  return {
    throttleRawMin: Math.round(safeNumber(source.throttle_raw_min, DEFAULT_CALIBRATION.throttleRawMin)),
    throttleRawMax: Math.round(safeNumber(source.throttle_raw_max, DEFAULT_CALIBRATION.throttleRawMax)),
    brakeRawMin: Math.round(safeNumber(source.brake_raw_min, DEFAULT_CALIBRATION.brakeRawMin)),
    brakeRawMax: Math.round(safeNumber(source.brake_raw_max, DEFAULT_CALIBRATION.brakeRawMax)),
    steeringCenter: Math.round(safeNumber(source.steering_center, DEFAULT_CALIBRATION.steeringCenter)),
    steeringLeft10: Math.round(safeNumber(source.steering_left_10, DEFAULT_CALIBRATION.steeringLeft10)),
    steeringRight10: Math.round(safeNumber(source.steering_right_10, DEFAULT_CALIBRATION.steeringRight10)),
    steeringLeftLimit: Math.round(safeNumber(source.steering_left_limit, DEFAULT_CALIBRATION.steeringLeftLimit)),
    steeringRightLimit: Math.round(safeNumber(source.steering_right_limit, DEFAULT_CALIBRATION.steeringRightLimit)),
    handwheelCenter: Math.round(safeNumber(source.handwheel_center, DEFAULT_CALIBRATION.handwheelCenter)),
    handwheelLeft10: Math.round(safeNumber(source.handwheel_left_10, DEFAULT_CALIBRATION.handwheelLeft10)),
    handwheelRight10: Math.round(safeNumber(source.handwheel_right_10, DEFAULT_CALIBRATION.handwheelRight10)),
    handwheelLeftLimit: Math.round(safeNumber(source.handwheel_left_limit, DEFAULT_CALIBRATION.handwheelLeftLimit)),
    handwheelRightLimit: Math.round(safeNumber(source.handwheel_right_limit, DEFAULT_CALIBRATION.handwheelRightLimit)),
    steeringEstimate: Math.round(safeNumber(source.steering_estimate, DEFAULT_CALIBRATION.steeringEstimate)),
    handwheelEstimate: Math.round(safeNumber(source.handwheel_estimate, DEFAULT_CALIBRATION.handwheelEstimate))
  };
}

export function normalizeStatusPayload(payload) {
  const source = payload || {};

  return {
    online: true,
    chassisType: String(source.chassis_type || source.ct || DEFAULT_APP_CONFIG.chassisType).toLowerCase(),
    driveAxle: String(source.drive_axle || source.da || DEFAULT_APP_CONFIG.driveAxle).toLowerCase(),
    driveMode: String(source.drive_mode || source.dm || '').toUpperCase(),
    localGear: String(source.gear || source.g || 'D').toUpperCase(),
    remoteGear: String(source.remote_gear || source.rg || 'N').toUpperCase(),
    controlOwner: String(source.control_owner || source.co || 'LOCAL').toUpperCase(),
    remoteMode: String(source.remote_mode || source.rm || 'MONITOR').toUpperCase(),
    remoteTakeoverEnabled: safeBool(source.remote_takeover_enabled || source.rto, false),
    localControlActive: safeBool(source.local_control_active || source.lca, false),
    vehicleMoving: safeBool(source.vehicle_moving || source.mov, false),
    vehicleMovingCommand: safeBool(source.vehicle_moving_command || source.mvc, false),
    vehicleMovingActual: safeBool(source.vehicle_moving_actual || source.mva, false),
    softStopActive: safeBool(source.soft_stop_active || source.ssa, false),
    emergencyStopActive: safeBool(source.emergency_stop_active || source.esa, false),
    hardwareEstopActive: safeBool(source.hardware_estop_active || source.hea, false),
    outputsEnabled: safeBool(source.outputs_enabled || source.oe, false),
    remoteActive: safeBool(source.remote_active || source.ra, false),
    remoteValid: safeBool(source.remote_valid || source.rv, false),
    driveFeedbackValid: safeBool(source.drive_feedback_valid || source.dfv, false),
    linearSteeringEnabled: safeBool(source.linear_steering_enabled || source.ls, false),
    linearSteeringDetected: safeBool(source.linear_steering_detected || source.lsd, false),
    throttleRaw: Math.round(safeNumber(source.throttle_raw || source.thr, 0)),
    brakeRaw: Math.round(safeNumber(source.brake_raw || source.brk, 0)),
    leftTargetRpm: Math.round(safeNumber(source.left_target_rpm || source.lr, 0)),
    rightTargetRpm: Math.round(safeNumber(source.right_target_rpm || source.rr, 0)),
    leftActualRpm: Math.round(safeNumber(source.left_actual_rpm || source.lar, 0)),
    rightActualRpm: Math.round(safeNumber(source.right_actual_rpm || source.rar, 0)),
    leftActualRpmValid: safeBool(source.left_actual_rpm_valid || source.lav, false),
    rightActualRpmValid: safeBool(source.right_actual_rpm_valid || source.rav, false),
    leftActualRpmAgeMs: Math.round(safeNumber(source.left_actual_rpm_age_ms || source.laa, 0)),
    rightActualRpmAgeMs: Math.round(safeNumber(source.right_actual_rpm_age_ms || source.raa, 0)),
    steerTargetRpm: Math.round(safeNumber(source.steer_target_rpm || source.sr, 0)),
    steeringFeedback: Math.round(safeNumber(source.steering_feedback || source.sf, 0)),
    steeringFeedbackValid: safeBool(source.steering_feedback_valid || source.sfv, false),
    steeringFeedbackAgeMs: Math.round(safeNumber(source.steering_feedback_age_ms || source.sfa, 0)),
    driveCanReady: safeBool(source.drive_can_ready || source.dc, false),
    steerCanReady: safeBool(source.steer_can_ready || source.sc, false),
    steerCanNodeId: Math.round(safeNumber(source.steer_can_node_id || source.sid, DEFAULT_APP_CONFIG.steerCanNodeId)),
    handwheelCanNodeId: Math.round(safeNumber(source.handwheel_can_node_id || source.hid, DEFAULT_APP_CONFIG.handwheelCanNodeId)),
    leftDriveInverted: safeBool(source.left_drive_inverted || source.ldi, DEFAULT_APP_CONFIG.leftDriveInverted),
    rightDriveInverted: safeBool(source.right_drive_inverted || source.rdi, DEFAULT_APP_CONFIG.rightDriveInverted),
    bleConnected: safeBool(source.ble_connected, false),
    remoteSource: String(source.remote_source || source.src || 'NONE').toUpperCase()
  };
}

export function normalizeCapabilitiesPayload(payload) {
  const source = payload || {};

  return {
    ackermannSupported: safeBool(source.ackermann_supported, true),
    diffSupported: safeBool(source.diff_supported, true),
    frontDriveSupported: safeBool(source.front_drive_supported, true),
    rearDriveSupported: safeBool(source.rear_drive_supported, true),
    linearSteeringSupported: safeBool(source.linear_steering_supported, false),
    pedalCalibrationSupported: safeBool(source.pedal_calibration_supported, true),
    steeringCalibrationSupported: safeBool(source.steering_calibration_supported, true),
    remoteTakeoverSupported: safeBool(source.remote_takeover_supported, true),
    remoteMonitorSupported: safeBool(source.remote_monitor_supported, true),
    softStopSupported: safeBool(source.soft_stop_supported, true),
    hardwareEstopSupported: safeBool(source.hardware_estop_supported, true),
    driveController: String(source.drive_controller || ''),
    steeringController: String(source.steering_controller || ''),
    driveCanReady: safeBool(source.drive_can_ready, false),
    steerCanReady: safeBool(source.steer_can_ready, false)
  };
}

export function normalizeLinearSteeringPayload(payload) {
  const source = payload || {};

  return {
    enabled: safeBool(source.enabled, false),
    detected: safeBool(source.detected, false),
    protocolSupported: safeBool(source.protocol_supported, false),
    steerCanReady: safeBool(source.steer_can_ready, false),
    steerCanRxCount: Math.round(safeNumber(source.steer_can_rx_count, 0)),
    steerCanLastRxMs: Math.round(safeNumber(source.steer_can_last_rx_ms, 0)),
    reason: String(source.reason || '')
  };
}
