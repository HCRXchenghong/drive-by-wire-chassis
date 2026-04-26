import {
  DEFAULT_APP_CONFIG,
  DEFAULT_CALIBRATION,
  normalizeCalibrationPayload,
  normalizeCapabilitiesPayload,
  normalizeConfigPayload,
  normalizeLinearSteeringPayload,
  normalizeStatusPayload,
  serializeCommand,
  serializeControlFrame
} from './chassis-protocol';

const runtimeState = {
  supported: false,
  platform: '',
  adapterReady: false,
  discovering: false,
  discoveredDevices: [],
  candidateFound: false,
  candidateId: '',
  candidateName: '',
  clientConnected: false,
  deviceId: '',
  deviceName: '',
  serviceId: '',
  rxCharacteristicId: '',
  txCharacteristicId: '',
  configCharacteristicId: '',
  notifyReady: false,
  mtu: 20,
  lastError: '',
  lastRxLine: '',
  lastRxAt: 0,
  txCount: 0,
  rxCount: 0,
  lastJson: null,
  lastJsonAt: 0,
  chassisStatus: null,
  chassisStatusAt: 0,
  chassisStatusSeq: 0,
  configData: { ...DEFAULT_APP_CONFIG },
  configAt: 0,
  statusData: null,
  statusAt: 0,
  capabilities: null,
  capabilitiesAt: 0,
  calibrationData: { ...DEFAULT_CALIBRATION },
  calibrationAt: 0,
  linearSteering: null,
  linearSteeringAt: 0,
  pedalSample: null,
  pedalSampleAt: 0,
  saveResult: null,
  saveResultAt: 0
};

let listenersBound = false;
let currentProfile = null;
let connectingDeviceId = '';
let incomingTextBuffer = '';
let writeQueue = [];
let writeBusy = false;
let sessionAutoConnect = true;

function updateState(patch) {
  Object.keys(patch).forEach((key) => {
    runtimeState[key] = patch[key];
  });
}

function cloneState() {
  return JSON.parse(JSON.stringify(runtimeState));
}

function resetState() {
  updateState({
    supported: false,
    platform: '',
    adapterReady: false,
    discovering: false,
    discoveredDevices: [],
    candidateFound: false,
    candidateId: '',
    candidateName: '',
    clientConnected: false,
    deviceId: '',
    deviceName: '',
    serviceId: '',
    rxCharacteristicId: '',
    txCharacteristicId: '',
    configCharacteristicId: '',
    notifyReady: false,
    mtu: 20,
    lastError: '',
    lastRxLine: '',
    lastRxAt: 0,
    txCount: 0,
    rxCount: 0,
    lastJson: null,
    lastJsonAt: 0,
    chassisStatus: null,
    chassisStatusAt: 0,
    chassisStatusSeq: 0,
    configData: { ...DEFAULT_APP_CONFIG },
    configAt: 0,
    statusData: null,
    statusAt: 0,
    capabilities: null,
    capabilitiesAt: 0,
    calibrationData: { ...DEFAULT_CALIBRATION },
    calibrationAt: 0,
    linearSteering: null,
    linearSteeringAt: 0,
    pedalSample: null,
    pedalSampleAt: 0,
    saveResult: null,
    saveResultAt: 0
  });
}

function updateDiscoveredDevice(device) {
  const nextList = (runtimeState.discoveredDevices || []).slice();
  const deviceId = String(device.deviceId || '');
  const name = getDeviceName(device);

  if (!deviceId || !name) {
    return;
  }

  const record = {
    deviceId,
    name,
    localName: String(device.localName || ''),
    rssi: typeof device.RSSI === 'number' ? device.RSSI : null
  };

  const existedIndex = nextList.findIndex((item) => item.deviceId === deviceId);
  if (existedIndex >= 0) {
    nextList.splice(existedIndex, 1, {
      ...nextList[existedIndex],
      ...record
    });
  } else {
    nextList.push(record);
  }

  nextList.sort((left, right) => {
    const leftRssi = typeof left.rssi === 'number' ? left.rssi : -999;
    const rightRssi = typeof right.rssi === 'number' ? right.rssi : -999;
    return rightRssi - leftRssi;
  });

  updateState({
    discoveredDevices: nextList
  });
}

function normalizeUuid(uuid) {
  return String(uuid || '').replace(/-/g, '').toUpperCase();
}

function getDeviceName(device) {
  return String((device && (device.name || device.localName)) || '');
}

function stringifyError(error) {
  if (!error) {
    return '';
  }

  if (typeof error === 'string') {
    return error;
  }

  if (typeof error.errMsg === 'string' && typeof error.errCode !== 'undefined') {
    return `${error.errMsg} (${error.errCode})`;
  }

  if (typeof error.errMsg === 'string') {
    return error.errMsg;
  }

  try {
    return JSON.stringify(error);
  } catch (innerError) {
    return String(error);
  }
}

function makeError(prefix, error) {
  const detail = stringifyError(error);
  return detail ? `${prefix}: ${detail}` : prefix;
}

function tryParseJsonLine(line) {
  if (!line || line.charAt(0) !== '{') {
    return null;
  }

  try {
    return JSON.parse(line);
  } catch (error) {
    return null;
  }
}

function isBleSupported() {
  return typeof uni !== 'undefined' && typeof uni.openBluetoothAdapter === 'function';
}

function clearWriteQueue() {
  writeQueue = [];
  writeBusy = false;
}

function stopDiscoveryQuietly() {
  try {
    uni.stopBluetoothDevicesDiscovery({});
  } catch (error) {}

  updateState({
    discovering: false
  });
}

function closeBleConnectionQuietly() {
  const deviceId = runtimeState.deviceId;

  if (!deviceId) {
    return;
  }

  try {
    uni.closeBLEConnection({
      deviceId
    });
  } catch (error) {}
}

function closeBluetoothAdapterQuietly() {
  try {
    uni.closeBluetoothAdapter({});
  } catch (error) {}
}

function asciiTextToArrayBuffer(text) {
  const source = String(text || '');
  const bytes = new Uint8Array(source.length);

  for (let index = 0; index < source.length; index += 1) {
    bytes[index] = source.charCodeAt(index) & 0xFF;
  }

  return bytes.buffer;
}

function arrayBufferToAsciiText(buffer) {
  const bytes = new Uint8Array(buffer);
  let text = '';

  for (let index = 0; index < bytes.length; index += 1) {
    text += String.fromCharCode(bytes[index]);
  }

  return text;
}

function splitTextToBuffers(text) {
  const source = String(text || '');
  const payloadLimit = Math.max(20, Math.min((runtimeState.mtu || 23) - 3, 244));
  const buffers = [];

  for (let offset = 0; offset < source.length; offset += payloadLimit) {
    buffers.push(asciiTextToArrayBuffer(source.slice(offset, offset + payloadLimit)));
  }

  return buffers;
}

function applyParsedMessage(parsed, textLine) {
  const now = Date.now();
  const patch = {
    lastRxLine: textLine,
    lastRxAt: now,
    rxCount: runtimeState.rxCount + 1,
    lastJson: parsed,
    lastJsonAt: now
  };

  if (!parsed || !parsed.cmd) {
    updateState(patch);
    return;
  }

  switch (String(parsed.cmd)) {
    case 'chassis_status':
      patch.chassisStatus = normalizeStatusPayload(parsed);
      patch.chassisStatusAt = now;
      patch.chassisStatusSeq = Number(parsed.seq || runtimeState.chassisStatusSeq + 1);
      patch.configData = {
        ...runtimeState.configData,
        ...normalizeConfigPayload(parsed)
      };
      break;
    case 'config_ack':
      patch.configData = normalizeConfigPayload(parsed);
      patch.configAt = now;
      break;
    case 'status_ack':
      patch.statusData = normalizeStatusPayload(parsed);
      patch.statusAt = now;
      break;
    case 'capability_status':
      patch.capabilities = normalizeCapabilitiesPayload(parsed);
      patch.capabilitiesAt = now;
      break;
    case 'calibration_status':
      patch.calibrationData = normalizeCalibrationPayload(parsed);
      patch.calibrationAt = now;
      break;
    case 'linear_steering_status':
      patch.linearSteering = normalizeLinearSteeringPayload(parsed);
      patch.linearSteeringAt = now;
      break;
    case 'pedal_sample_result':
      patch.pedalSample = parsed;
      patch.pedalSampleAt = now;
      break;
    case 'save_result':
      patch.saveResult = parsed;
      patch.saveResultAt = now;
      break;
    default:
      break;
  }

  updateState(patch);
}

function processIncomingLine(line) {
  const textLine = String(line || '').trim();

  if (!textLine) {
    return;
  }

  const parsed = tryParseJsonLine(textLine);
  if (parsed) {
    applyParsedMessage(parsed, textLine);
    return;
  }

  updateState({
    lastRxLine: textLine,
    lastRxAt: Date.now(),
    rxCount: runtimeState.rxCount + 1
  });
}

function consumeIncomingChunk(text) {
  incomingTextBuffer += String(text || '').replace(/\r/g, '');

  while (incomingTextBuffer.indexOf('\n') !== -1) {
    const splitIndex = incomingTextBuffer.indexOf('\n');
    const line = incomingTextBuffer.slice(0, splitIndex);
    incomingTextBuffer = incomingTextBuffer.slice(splitIndex + 1);
    processIncomingLine(line);
  }

  if (incomingTextBuffer.length > 1024) {
    incomingTextBuffer = incomingTextBuffer.slice(-512);
  }
}

function handleWriteFailure(error) {
  clearWriteQueue();
  updateState({
    lastError: makeError('BLE 发送失败', error),
    clientConnected: false,
    notifyReady: false
  });
}

function flushWriteQueue() {
  if (writeBusy || !runtimeState.clientConnected || !runtimeState.notifyReady) {
    return;
  }

  if (writeQueue.length === 0) {
    return;
  }

  const nextChunk = writeQueue.shift();
  writeBusy = true;

  uni.writeBLECharacteristicValue({
    deviceId: runtimeState.deviceId,
    serviceId: runtimeState.serviceId,
    characteristicId: runtimeState.txCharacteristicId,
    value: nextChunk,
    success() {
      updateState({
        txCount: runtimeState.txCount + 1
      });

      writeBusy = false;
      setTimeout(() => {
        flushWriteQueue();
      }, 8);
    },
    fail(error) {
      writeBusy = false;
      handleWriteFailure(error);
    }
  });
}

function enqueueTextLine(text, replacePendingQueue) {
  if (replacePendingQueue) {
    writeQueue = [];
  }

  splitTextToBuffers(text).forEach((buffer) => {
    writeQueue.push(buffer);
  });

  flushWriteQueue();
}

function requestPreferredMtu() {
  if (!runtimeState.deviceId || typeof uni.setBLEMTU !== 'function') {
    return;
  }

  uni.setBLEMTU({
    deviceId: runtimeState.deviceId,
    mtu: 247,
    success() {
      updateState({
        mtu: 247
      });
    },
    fail() {
      updateState({
        mtu: 20
      });
    }
  });
}

function enableNotifyForBridge() {
  uni.notifyBLECharacteristicValueChange({
    deviceId: runtimeState.deviceId,
    serviceId: runtimeState.serviceId,
    characteristicId: runtimeState.rxCharacteristicId,
    state: true,
    success() {
      updateState({
        notifyReady: true,
        lastError: ''
      });
      requestPreferredMtu();
    },
    fail(error) {
      updateState({
        lastError: makeError('打开 BLE 透传通知失败', error)
      });
    }
  });
}

function inspectServiceCharacteristics(services, index) {
  if (index >= services.length) {
    updateState({
      lastError: '未找到 EWM22 透传特征，请确认模块工作在 BLE 透传模式。'
    });
    return;
  }

  const serviceId = services[index].uuid;

  uni.getBLEDeviceCharacteristics({
    deviceId: runtimeState.deviceId,
    serviceId,
    success(result) {
      const characteristics = result.characteristics || [];
      let rxCharacteristicId = '';
      let txCharacteristicId = '';
      let configCharacteristicId = '';

      characteristics.forEach((item) => {
        const normalized = normalizeUuid(item.uuid);
        const properties = item.properties || {};

        if (!rxCharacteristicId && normalized.endsWith('FFF1') && properties.notify) {
          rxCharacteristicId = item.uuid;
        }

        if (!txCharacteristicId &&
            normalized.endsWith('FFF2') &&
            (properties.write || properties.writeNoResponse)) {
          txCharacteristicId = item.uuid;
        }

        if (!configCharacteristicId && normalized.endsWith('FFF3')) {
          configCharacteristicId = item.uuid;
        }
      });

      if (rxCharacteristicId && txCharacteristicId) {
        updateState({
          serviceId,
          rxCharacteristicId,
          txCharacteristicId,
          configCharacteristicId
        });
        enableNotifyForBridge();
        return;
      }

      inspectServiceCharacteristics(services, index + 1);
    },
    fail() {
      inspectServiceCharacteristics(services, index + 1);
    }
  });
}

function discoverBridgeCharacteristics() {
  uni.getBLEDeviceServices({
    deviceId: runtimeState.deviceId,
    success(result) {
      inspectServiceCharacteristics(result.services || [], 0);
    },
    fail(error) {
      updateState({
        lastError: makeError('读取 BLE 服务失败', error)
      });
    }
  });
}

function connectToMatchedDevice(deviceId, deviceName) {
  if (!deviceId || connectingDeviceId || runtimeState.clientConnected) {
    return;
  }

  connectingDeviceId = deviceId;
  stopDiscoveryQuietly();

  uni.createBLEConnection({
    deviceId,
    timeout: 15000,
    success() {
      connectingDeviceId = '';
      updateState({
        clientConnected: true,
        deviceId,
        deviceName,
        lastError: ''
      });
      discoverBridgeCharacteristics();
    },
    fail(error) {
      connectingDeviceId = '';
      updateState({
        lastError: makeError('连接 BLE 设备失败', error)
      });
    }
  });
}

function findDiscoveredDeviceById(deviceId) {
  return (runtimeState.discoveredDevices || []).find((item) => item.deviceId === deviceId) || null;
}

function matchesProfile(deviceName) {
  const name = String(deviceName || '');

  if (!currentProfile) {
    return false;
  }

  const exactName = String(currentProfile.bleNameExact || '');
  if (exactName && name === exactName) {
    return true;
  }

  const prefix = String(currentProfile.bleNamePrefix || '');
  if (prefix && name.indexOf(prefix) === 0) {
    return true;
  }

  const keyword = String(currentProfile.bleNameKeyword || '');
  if (keyword && name.indexOf(keyword) !== -1) {
    return true;
  }

  return false;
}

function registerListenersOnce() {
  if (listenersBound) {
    return;
  }

  listenersBound = true;

  uni.onBluetoothDeviceFound((result) => {
    const devices = result.devices || [];

    devices.forEach((device) => {
      const deviceName = getDeviceName(device);

      if (!deviceName) {
        return;
      }

      if (!matchesProfile(deviceName)) {
        return;
      }

      updateDiscoveredDevice(device);
      updateState({
        candidateFound: true,
        candidateId: device.deviceId,
        candidateName: deviceName,
        lastError: ''
      });

      if (sessionAutoConnect) {
        connectToMatchedDevice(device.deviceId, deviceName);
      }
    });
  });

  uni.onBLEConnectionStateChange((result) => {
    if (result.deviceId !== runtimeState.deviceId) {
      return;
    }

    if (result.connected) {
      return;
    }

    clearWriteQueue();
    incomingTextBuffer = '';

    updateState({
      clientConnected: false,
      notifyReady: false,
      serviceId: '',
      rxCharacteristicId: '',
      txCharacteristicId: '',
      configCharacteristicId: '',
      lastError: runtimeState.lastError || 'BLE 链路已断开'
    });
  });

  uni.onBLECharacteristicValueChange((result) => {
    if (result.deviceId !== runtimeState.deviceId) {
      return;
    }

    if (normalizeUuid(result.characteristicId) !== normalizeUuid(runtimeState.rxCharacteristicId)) {
      return;
    }

    consumeIncomingChunk(arrayBufferToAsciiText(result.value));
  });
}

function startDiscovery() {
  uni.startBluetoothDevicesDiscovery({
    allowDuplicatesKey: false,
    success() {
      updateState({
        discovering: true,
        lastError: ''
      });
    },
    fail(error) {
      updateState({
        lastError: makeError('启动 BLE 扫描失败', error)
      });
    }
  });
}

export function getBridgeState() {
  return cloneState();
}

export function startBleSession(profile, options) {
  resetState();
  clearWriteQueue();
  incomingTextBuffer = '';
  currentProfile = profile || null;
  connectingDeviceId = '';
  sessionAutoConnect = !(options && options.autoConnect === false);

  if (!isBleSupported()) {
    updateState({
      supported: false,
      platform: 'unsupported',
      lastError: '当前环境不支持 uni-app BLE，请运行到 Android 真机 App。'
    });

    return {
      ok: false,
      error: runtimeState.lastError
    };
  }

  registerListenersOnce();

  uni.openBluetoothAdapter({
    success() {
      updateState({
        supported: true,
        platform: 'uni-app BLE',
        adapterReady: true,
        lastError: ''
      });
      startDiscovery();
    },
    fail(error) {
      updateState({
        supported: true,
        platform: 'uni-app BLE',
        lastError: makeError('蓝牙适配器打开失败，请先开启手机蓝牙并授权定位/蓝牙权限', error)
      });
    }
  });

  return {
    ok: true
  };
}

export function stopBleSession() {
  clearWriteQueue();
  incomingTextBuffer = '';
  connectingDeviceId = '';
  currentProfile = null;
  sessionAutoConnect = true;

  stopDiscoveryQuietly();
  closeBleConnectionQuietly();
  closeBluetoothAdapterQuietly();
  resetState();
}

export function sendTextLine(text, options) {
  if (!runtimeState.clientConnected || !runtimeState.notifyReady || !runtimeState.txCharacteristicId) {
    return {
      ok: false,
      error: 'BLE 透传链路尚未就绪'
    };
  }

  enqueueTextLine(String(text || ''), !!(options && options.replacePendingQueue));

  return {
    ok: true
  };
}

export function sendControlFrame(frame) {
  return sendTextLine(serializeControlFrame(frame), {
    replacePendingQueue: true
  });
}

export function sendJsonCommand(command, options) {
  return sendTextLine(serializeCommand(command), {
    replacePendingQueue: !!(options && options.replacePendingQueue)
  });
}

export function connectBleDevice(deviceId) {
  const target = findDiscoveredDeviceById(deviceId);

  if (!target) {
    return {
      ok: false,
      error: '未找到所选蓝牙设备，请重新扫描'
    };
  }

  connectToMatchedDevice(target.deviceId, target.name);

  return {
    ok: true
  };
}
