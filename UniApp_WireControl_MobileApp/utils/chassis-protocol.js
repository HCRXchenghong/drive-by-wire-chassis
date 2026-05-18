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
  driveMaxRpm: 500,
  steeringMaxRpm: 2000,
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
  steeringLeft10: 100,
  steeringRight10: -100,
  steeringLeftLimit: 600,
  steeringRightLimit: -600,
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

function firstPresent(source, keys) {
  const safe = source || {};
  for (let index = 0; index < keys.length; index += 1) {
    const key = keys[index];
    if (Object.prototype.hasOwnProperty.call(safe, key) &&
        safe[key] !== undefined &&
        safe[key] !== null &&
        safe[key] !== '') {
      return safe[key];
    }
  }

  return undefined;
}

function pickString(source, keys, fallback = '') {
  const value = firstPresent(source, keys);
  return String(value === undefined ? fallback : value);
}

function pickNumber(source, keys, fallback) {
  const value = firstPresent(source, keys);
  return safeNumber(value === undefined ? fallback : value, fallback);
}

function pickRoundedNumber(source, keys, fallback) {
  return Math.round(pickNumber(source, keys, fallback));
}

function pickBool(source, keys, fallback) {
  const value = firstPresent(source, keys);
  return safeBool(value === undefined ? fallback : value, fallback);
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

function utf8BytesToText(bytes) {
  const source = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes || 0);
  let text = '';
  let index = 0;

  while (index < source.length) {
    const first = source[index];

    if (first < 0x80) {
      text += String.fromCharCode(first);
      index += 1;
      continue;
    }

    if ((first & 0xE0) === 0xC0 && (index + 1) < source.length) {
      const second = source[index + 1];
      if ((second & 0xC0) === 0x80) {
        text += String.fromCharCode(((first & 0x1F) << 6) | (second & 0x3F));
        index += 2;
        continue;
      }
    }

    if ((first & 0xF0) === 0xE0 && (index + 2) < source.length) {
      const second = source[index + 1];
      const third = source[index + 2];
      if (((second & 0xC0) === 0x80) && ((third & 0xC0) === 0x80)) {
        text += String.fromCharCode(
          ((first & 0x0F) << 12) |
          ((second & 0x3F) << 6) |
          (third & 0x3F)
        );
        index += 3;
        continue;
      }
    }

    if ((first & 0xF8) === 0xF0 && (index + 3) < source.length) {
      const second = source[index + 1];
      const third = source[index + 2];
      const fourth = source[index + 3];
      if (((second & 0xC0) === 0x80) &&
          ((third & 0xC0) === 0x80) &&
          ((fourth & 0xC0) === 0x80)) {
        const codePoint = ((first & 0x07) << 18) |
                          ((second & 0x3F) << 12) |
                          ((third & 0x3F) << 6) |
                          (fourth & 0x3F);
        const safeCodePoint = codePoint - 0x10000;
        text += String.fromCharCode(
          0xD800 + ((safeCodePoint >> 10) & 0x3FF),
          0xDC00 + (safeCodePoint & 0x3FF)
        );
        index += 4;
        continue;
      }
    }

    text += '\uFFFD';
    index += 1;
  }

  return text;
}

function maybeRepairUtf8Mojibake(text) {
  const source = String(text || '');

  if (!source) {
    return '';
  }

  if (/[\u4E00-\u9FFF]/.test(source)) {
    return source;
  }

  if (!/[ÃÂÆÉæèéêîïðñåç]/.test(source)) {
    return source;
  }

  const bytes = new Uint8Array(source.length);
  for (let index = 0; index < source.length; index += 1) {
    bytes[index] = source.charCodeAt(index) & 0xFF;
  }

  const repaired = utf8BytesToText(bytes).trim();
  return /[\u4E00-\u9FFF]/.test(repaired) ? repaired : source;
}

function faultCodeToZhMessage(faultCode) {
  switch (String(faultCode || 'none')) {
    case 'drive_reply_timeout':
      return '驱动 CAN 回复超时，控制不中断，请检查驱动回读';
    case 'steer_reply_timeout':
      return '机械转向 CAN 回复超时，已禁止行驶并等待自动恢复';
    case 'handwheel_reply_timeout':
      return '线性方向盘回复超时，已禁用线性方向盘相关功能';
    case 'drive_bus_error':
      return '驱动 CAN 总线异常，已锁停并等待自动恢复';
    case 'steer_bus_error':
      return '转向 CAN 总线异常，已禁止行驶并等待自动恢复';
    case 'steering_feedback_lost':
      return '机械转向真实反馈丢失，已禁止行驶';
    default:
      return '';
  }
}

function normalizeFaultMessageZh(source) {
  const faultCode = pickString(source, ['fault_code', 'fc'], 'none');
  const rawMessage = maybeRepairUtf8Mojibake(pickString(source, ['fault_message_zh', 'fm'], '')).trim();

  if (!rawMessage || rawMessage === faultCode) {
    return faultCodeToZhMessage(faultCode);
  }

  return rawMessage;
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
    drive_max_rpm: clamp(Math.round(safeNumber(safe.driveMaxRpm, DEFAULT_APP_CONFIG.driveMaxRpm)), 50, 5000),
    steering_max_rpm: clamp(Math.round(safeNumber(safe.steeringMaxRpm, DEFAULT_APP_CONFIG.steeringMaxRpm)), 200, 5000),
    steer_can_node_id: clamp(Math.round(safeNumber(safe.steerCanNodeId, DEFAULT_APP_CONFIG.steerCanNodeId)), 1, 0x7F),
    handwheel_can_node_id: clamp(Math.round(safeNumber(safe.handwheelCanNodeId, DEFAULT_APP_CONFIG.handwheelCanNodeId)), 1, 0x7F),
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
    chassisType: pickString(source, ['chassis_type', 'ct'], DEFAULT_APP_CONFIG.chassisType).toLowerCase() === 'diff' ? 'diff' : 'ackermann',
    driveAxle: pickString(source, ['drive_axle', 'da'], DEFAULT_APP_CONFIG.driveAxle).toLowerCase() === 'fwd' ? 'fwd' : 'rwd',
    frontTrackMm: pickRoundedNumber(source, ['front_track_mm', 'ft'], DEFAULT_APP_CONFIG.frontTrackMm),
    wheelbaseMm: pickRoundedNumber(source, ['wheelbase_mm', 'wb'], DEFAULT_APP_CONFIG.wheelbaseMm),
    rearTrackMm: pickRoundedNumber(source, ['rear_track_mm', 'rt'], DEFAULT_APP_CONFIG.rearTrackMm),
    driveMaxRpm: pickRoundedNumber(source, ['drive_max_rpm', 'dmr'], DEFAULT_APP_CONFIG.driveMaxRpm),
    steeringMaxRpm: pickRoundedNumber(source, ['steering_max_rpm', 'smr'], DEFAULT_APP_CONFIG.steeringMaxRpm),
    steerCanNodeId: pickRoundedNumber(source, ['steer_can_node_id', 'sid'], DEFAULT_APP_CONFIG.steerCanNodeId),
    handwheelCanNodeId: pickRoundedNumber(source, ['handwheel_can_node_id', 'hid'], DEFAULT_APP_CONFIG.handwheelCanNodeId),
    leftDriveInverted: pickBool(source, ['left_drive_inverted', 'ldi'], DEFAULT_APP_CONFIG.leftDriveInverted),
    rightDriveInverted: pickBool(source, ['right_drive_inverted', 'rdi'], DEFAULT_APP_CONFIG.rightDriveInverted),
    hasLinearSteering: pickBool(source, ['linear_steering_enabled', 'ls'], DEFAULT_APP_CONFIG.hasLinearSteering),
    pedalConfig: pickString(source, ['pedal_config'], DEFAULT_APP_CONFIG.pedalConfig).toLowerCase() === 'estop_throttle'
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
  const faultCode = pickString(source, ['fault_code', 'fc'], 'none');
  const faultDomain = pickString(source, ['fault_domain', 'fd'], 'none');
  const driveMaxRpm = firstPresent(source, ['drive_max_rpm', 'dmr']);
  const steeringMaxRpm = firstPresent(source, ['steering_max_rpm', 'smr']);

  return {
    online: true,
    chassisType: pickString(source, ['chassis_type', 'ct'], DEFAULT_APP_CONFIG.chassisType).toLowerCase(),
    driveAxle: pickString(source, ['drive_axle', 'da'], DEFAULT_APP_CONFIG.driveAxle).toLowerCase(),
    driveMode: pickString(source, ['drive_mode', 'dm'], '').toUpperCase(),
    driveMaxRpm: driveMaxRpm === undefined
      ? undefined
      : Math.round(safeNumber(driveMaxRpm, DEFAULT_APP_CONFIG.driveMaxRpm)),
    steeringMaxRpm: steeringMaxRpm === undefined
      ? undefined
      : Math.round(safeNumber(steeringMaxRpm, DEFAULT_APP_CONFIG.steeringMaxRpm)),
    localGear: pickString(source, ['gear', 'g'], 'D').toUpperCase(),
    remoteGear: pickString(source, ['remote_gear', 'rg'], 'N').toUpperCase(),
    controlOwner: pickString(source, ['control_owner', 'co'], 'LOCAL').toUpperCase(),
    remoteMode: pickString(source, ['remote_mode', 'rm'], 'MONITOR').toUpperCase(),
    remoteTakeoverEnabled: pickBool(source, ['remote_takeover_enabled', 'rto'], false),
    localControlActive: pickBool(source, ['local_control_active', 'lca'], false),
    vehicleMoving: pickBool(source, ['vehicle_moving', 'mov'], false),
    vehicleMovingCommand: pickBool(source, ['vehicle_moving_command', 'mvc'], false),
    vehicleMovingActual: pickBool(source, ['vehicle_moving_actual', 'mva'], false),
    softStopActive: pickBool(source, ['soft_stop_active', 'ssa'], false),
    emergencyStopActive: pickBool(source, ['emergency_stop_active', 'esa'], false),
    hardwareEstopActive: pickBool(source, ['hardware_estop_active', 'hea'], false),
    outputsEnabled: pickBool(source, ['outputs_enabled', 'oe'], false),
    outputsLockedByFault: pickBool(source, ['outputs_locked_by_fault', 'olf'], false),
    remoteActive: pickBool(source, ['remote_active', 'ra'], false),
    remoteValid: pickBool(source, ['remote_valid', 'rv'], false),
    driveFeedbackValid: pickBool(source, ['drive_feedback_valid', 'dfv'], false),
    faultCode,
    faultDomain,
    faultMessageZh: normalizeFaultMessageZh(source),
    driveCanFault: pickBool(source, ['drive_can_fault', 'dcf'], false),
    steerCanFault: pickBool(source, ['steer_can_fault', 'scf'], false),
    handwheelCanFault: pickBool(source, ['handwheel_can_fault', 'hcf'], false),
    canRecoveryActive: pickBool(source, ['can_recovery_active', 'cra'], false),
    can1BusOffCount: pickRoundedNumber(source, ['can1_bus_off_count', 'c1bo'], 0),
    can2BusOffCount: pickRoundedNumber(source, ['can2_bus_off_count', 'c2bo'], 0),
    can1LastErrorCode: pickRoundedNumber(source, ['can1_last_error_code', 'c1lec'], 0),
    can2LastErrorCode: pickRoundedNumber(source, ['can2_last_error_code', 'c2lec'], 0),
    linearSteeringEnabled: pickBool(source, ['linear_steering_enabled', 'ls'], false),
    linearSteeringDetected: pickBool(source, ['linear_steering_detected', 'lsd'], false),
    throttleRaw: pickRoundedNumber(source, ['throttle_raw', 'thr'], 0),
    brakeRaw: pickRoundedNumber(source, ['brake_raw', 'brk'], 0),
    leftTargetRpm: pickRoundedNumber(source, ['left_target_rpm', 'lr'], 0),
    rightTargetRpm: pickRoundedNumber(source, ['right_target_rpm', 'rr'], 0),
    leftActualRpm: pickRoundedNumber(source, ['left_actual_rpm', 'lar'], 0),
    rightActualRpm: pickRoundedNumber(source, ['right_actual_rpm', 'rar'], 0),
    leftActualRpmValid: pickBool(source, ['left_actual_rpm_valid', 'lav'], false),
    rightActualRpmValid: pickBool(source, ['right_actual_rpm_valid', 'rav'], false),
    leftActualRpmAgeMs: pickRoundedNumber(source, ['left_actual_rpm_age_ms', 'laa'], 0),
    rightActualRpmAgeMs: pickRoundedNumber(source, ['right_actual_rpm_age_ms', 'raa'], 0),
    steerTargetRpm: pickRoundedNumber(source, ['steer_target_rpm', 'sr'], 0),
    steerTargetRaw: pickRoundedNumber(source, ['steer_target_raw', 'str'], 0),
    steerTargetAngleDeg: pickNumber(source, ['steer_target_angle_deg', 'sta'], 0),
    steerActualAngleDeg: pickNumber(source, ['steer_actual_angle_deg', 'saa'], 0),
    steeringFeedback: pickRoundedNumber(source, ['steering_feedback', 'sf'], 0),
    steeringFeedbackValid: pickBool(source, ['steering_feedback_valid', 'sfv'], false),
    steeringFeedbackAgeMs: pickRoundedNumber(source, ['steering_feedback_age_ms', 'sfa'], 0),
    driveCanReady: pickBool(source, ['drive_can_ready', 'dc'], false),
    steerCanReady: pickBool(source, ['steer_can_ready', 'sc'], false),
    steerCanNodeId: pickRoundedNumber(source, ['steer_can_node_id', 'sid'], DEFAULT_APP_CONFIG.steerCanNodeId),
    handwheelCanNodeId: pickRoundedNumber(source, ['handwheel_can_node_id', 'hid'], DEFAULT_APP_CONFIG.handwheelCanNodeId),
    leftDriveInverted: pickBool(source, ['left_drive_inverted', 'ldi'], DEFAULT_APP_CONFIG.leftDriveInverted),
    rightDriveInverted: pickBool(source, ['right_drive_inverted', 'rdi'], DEFAULT_APP_CONFIG.rightDriveInverted),
    bleConnected: pickBool(source, ['ble_connected'], false),
    remoteSource: pickString(source, ['remote_source', 'src'], 'NONE').toUpperCase(),
    remoteAgeMs: pickRoundedNumber(source, ['remote_age_ms', 'ra'], 0),
    remoteFrameCount: pickRoundedNumber(source, ['remote_frame_count', 'rfc'], 0),
    remoteCrcErrorCount: pickRoundedNumber(source, ['remote_crc_error_count', 'rce'], 0),
    remoteParseErrorCount: pickRoundedNumber(source, ['remote_parse_error_count', 'rpe'], 0),
    remoteOverflowErrorCount: pickRoundedNumber(source, ['remote_overflow_error_count', 'roe'], 0)
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
    driveController: pickString(source, ['drive_controller'], ''),
    steeringController: pickString(source, ['steering_controller'], ''),
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
    reason: pickString(source, ['reason'], '')
  };
}
