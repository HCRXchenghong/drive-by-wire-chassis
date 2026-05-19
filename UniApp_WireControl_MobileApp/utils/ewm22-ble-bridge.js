import {
  normalizeCalibrationPayload,
  normalizeCapabilitiesPayload,
  normalizeConfigPayload,
  normalizeLinearSteeringPayload,
  normalizeStatusPayload,
  serializeCommand,
  serializeControlFrame
} from './chassis-protocol';

const BLE_DISCOVERY_INITIAL_DELAY_MS = 350;
const BLE_DISCOVERY_RETRY_DELAY_MS = 450;
const BLE_DISCOVERY_MAX_ATTEMPTS = 3;
const BLE_NOTIFY_RETRY_DELAY_MS = 300;
const BLE_NOTIFY_MAX_ATTEMPTS = 3;
const BLE_WRITE_RETRY_DELAY_MS = 40;
const BLE_WRITE_MAX_RETRIES = 2;

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
  notifyReadyAt: 0,
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
  configData: null,
  configAt: 0,
  configResult: null,
  configResultAt: 0,
  statusData: null,
  statusAt: 0,
  capabilities: null,
  capabilitiesAt: 0,
  calibrationData: null,
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
let nextWriteLineToken = 1;
let activeWriteLineToken = 0;
let sessionAutoConnect = true;
let discoveryRetryTimer = null;
let discoveryRequestToken = 0;
let notifyRetryTimer = null;

function scheduleFlushWriteQueue(delayMs) {
  setTimeout(() => {
    flushWriteQueue();
  }, Math.max(0, Number(delayMs) || 0));
}

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
    notifyReadyAt: 0,
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
    configData: null,
    configAt: 0,
    configResult: null,
    configResultAt: 0,
    statusData: null,
    statusAt: 0,
    capabilities: null,
    capabilitiesAt: 0,
    calibrationData: null,
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

function clearDiscoveryRetryTimer() {
  if (!discoveryRetryTimer) {
    return;
  }

  clearTimeout(discoveryRetryTimer);
  discoveryRetryTimer = null;
}

function clearNotifyRetryTimer() {
  if (!notifyRetryTimer) {
    return;
  }

  clearTimeout(notifyRetryTimer);
  notifyRetryTimer = null;
}

function invalidateDiscoveryRequests() {
  discoveryRequestToken += 1;
  clearDiscoveryRetryTimer();
  clearNotifyRetryTimer();
}

function isDiscoveryRequestActive(deviceId, token) {
  return !!deviceId &&
         !!runtimeState.clientConnected &&
         runtimeState.deviceId === deviceId &&
         token === discoveryRequestToken;
}

function describeCharacteristicProperties(properties) {
  const flags = [];
  const safeProperties = properties || {};

  if (safeProperties.notify) {
    flags.push('notify');
  }

  if (safeProperties.indicate) {
    flags.push('indicate');
  }

  if (safeProperties.read) {
    flags.push('read');
  }

  if (safeProperties.write) {
    flags.push('write');
  }

  if (safeProperties.writeNoResponse) {
    flags.push('writeNoRsp');
  }

  return flags.length > 0 ? flags.join('/') : 'none';
}

function getUuidShortCode(uuid) {
  const normalized = normalizeUuid(uuid);

  if (!normalized) {
    return '';
  }

  if (normalized.length === 4) {
    return normalized;
  }

  if (normalized.length >= 8 && normalized.indexOf('0000') === 0) {
    return normalized.slice(4, 8);
  }

  return normalized.length >= 4 ? normalized.slice(-4) : normalized;
}

function uuidMatchesShortCode(uuid, shortCode) {
  return getUuidShortCode(uuid) === String(shortCode || '').toUpperCase();
}

function summarizeCharacteristics(characteristics) {
  const list = Array.isArray(characteristics) ? characteristics : [];

  if (list.length === 0) {
    return '无特征';
  }

  return list.map((item) => {
    const label = getUuidShortCode(item.uuid);
    return `${label}[${describeCharacteristicProperties(item.properties)}]`;
  }).join(', ');
}

function makeDiscoveryErrorMessage(attempt, diagnostics) {
  const safeDiagnostics = diagnostics || {};
  const parts = [];

  parts.push(`BLE 特征发现第 ${attempt}/${BLE_DISCOVERY_MAX_ATTEMPTS} 次失败`);

  if (safeDiagnostics.stage === 'services') {
    parts.push(`读取服务失败：${safeDiagnostics.detail || '未知错误'}`);
  } else if (safeDiagnostics.stage === 'characteristics') {
    parts.push(`未找到完整透传特征`);

    if (safeDiagnostics.serviceSummaries && safeDiagnostics.serviceSummaries.length > 0) {
      parts.push(`已检查服务：${safeDiagnostics.serviceSummaries.join(' ; ')}`);
    }

    if (safeDiagnostics.characteristicErrors && safeDiagnostics.characteristicErrors.length > 0) {
      parts.push(`特征读取错误：${safeDiagnostics.characteristicErrors.join(' ; ')}`);
    }

    if (safeDiagnostics.missing) {
      parts.push(`缺失项：${safeDiagnostics.missing}`);
    }
  } else {
    parts.push(safeDiagnostics.detail || '未知错误');
  }

  return parts.join('，');
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
  activeWriteLineToken = 0;
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
        const codePoint = ((first & 0x1F) << 6) | (second & 0x3F);
        text += String.fromCharCode(codePoint);
        index += 2;
        continue;
      }
    }

    if ((first & 0xF0) === 0xE0 && (index + 2) < source.length) {
      const second = source[index + 1];
      const third = source[index + 2];
      if (((second & 0xC0) === 0x80) && ((third & 0xC0) === 0x80)) {
        const codePoint = ((first & 0x0F) << 12) |
                          ((second & 0x3F) << 6) |
                          (third & 0x3F);
        text += String.fromCharCode(codePoint);
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

function arrayBufferToAsciiText(buffer) {
  if (typeof TextDecoder !== 'undefined') {
    try {
      return new TextDecoder('utf-8').decode(buffer);
    } catch (error) {}
  }

  return utf8BytesToText(new Uint8Array(buffer));
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
      break;
    case 'config_ack':
      patch.configResult = parsed;
      patch.configResultAt = now;
      if (parsed.ok !== false) {
        patch.configData = normalizeConfigPayload(parsed);
        patch.configAt = now;
      }
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

  // The STM32 status / chassis_status payloads can be several hundred bytes
  // each, and BLE MTU=20 splits them into many notifications.  We must keep
  // a large enough window so a fragmented line is never chopped in half
  // before its terminating \n arrives, otherwise JSON.parse fails and the
  // handshake page stalls waiting for a reply.
  if (incomingTextBuffer.length > 8192) {
    incomingTextBuffer = incomingTextBuffer.slice(-4096);
  }
}

function handleWriteFailure(error) {
  updateState({
    lastError: makeError('BLE 发送失败', error)
  });
}

function writeQueueHasLine(lineToken) {
  return !!lineToken && writeQueue.some((chunk) => chunk.lineToken === lineToken);
}

function dropQueuedLine(lineToken) {
  if (!lineToken) {
    return;
  }

  writeQueue = writeQueue.filter((chunk) => chunk.lineToken !== lineToken);
}

function finishActiveLineIfDrained(lineToken) {
  if (activeWriteLineToken === lineToken && !writeQueueHasLine(lineToken)) {
    activeWriteLineToken = 0;
  }
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
  activeWriteLineToken = nextChunk.lineToken || 0;

  uni.writeBLECharacteristicValue({
    deviceId: runtimeState.deviceId,
    serviceId: runtimeState.serviceId,
    characteristicId: runtimeState.txCharacteristicId,
    value: nextChunk.buffer,
    success() {
      updateState({
        txCount: runtimeState.txCount + 1
      });

      writeBusy = false;
      finishActiveLineIfDrained(nextChunk.lineToken);
      scheduleFlushWriteQueue(8);
    },
    fail(error) {
      writeBusy = false;
      if ((nextChunk.retryCount || 0) < BLE_WRITE_MAX_RETRIES &&
          runtimeState.clientConnected &&
          runtimeState.notifyReady) {
        nextChunk.retryCount = (nextChunk.retryCount || 0) + 1;
        writeQueue.unshift(nextChunk);
        updateState({
          lastError: `${makeError('BLE 发送失败', error)}，正在重试 ${nextChunk.retryCount}/${BLE_WRITE_MAX_RETRIES}`
        });
        scheduleFlushWriteQueue(BLE_WRITE_RETRY_DELAY_MS);
        return;
      }

      handleWriteFailure(error);
      dropQueuedLine(nextChunk.lineToken);
      finishActiveLineIfDrained(nextChunk.lineToken);
      scheduleFlushWriteQueue(BLE_WRITE_RETRY_DELAY_MS);
    }
  });
}

function enqueueTextLine(text, options) {
  const settings = typeof options === 'object'
    ? options
    : { replacePendingQueue: !!options };
  const replacePendingQueue = !!settings.replacePendingQueue;
  const lineToken = nextWriteLineToken++;

  if (nextWriteLineToken > Number.MAX_SAFE_INTEGER - 1000) {
    nextWriteLineToken = 1;
  }

  if (replacePendingQueue) {
    const protectedLineToken = activeWriteLineToken;
    writeQueue = writeQueue.filter((chunk) => (
      protectedLineToken && chunk.lineToken === protectedLineToken
    ));
  }

  splitTextToBuffers(text).forEach((buffer) => {
    writeQueue.push({
      buffer,
      retryCount: 0,
      lineToken,
      priority: settings.priority || 'command'
    });
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

function isBridgeNotifyCharacteristic(characteristicId) {
  const normalized = normalizeUuid(characteristicId);

  return normalized &&
         (normalized === normalizeUuid(runtimeState.rxCharacteristicId) ||
          normalized === normalizeUuid(runtimeState.configCharacteristicId));
}

function enableNotifyForBridge() {
  const notifyTargets = [];
  let completed = 0;
  let successCount = 0;
  const errors = [];

  function finishNotifyAttempt() {
    completed += 1;

    if (completed < notifyTargets.length) {
      return;
    }

    if (successCount > 0) {
      updateState({
        notifyReady: true,
        lastError: ''
      });
      requestPreferredMtu();
      return;
    }

    updateState({
      lastError: errors.join('；') || '打开 BLE 透传通知失败'
    });
  }

  if (runtimeState.rxCharacteristicId) {
    notifyTargets.push({
      characteristicId: runtimeState.rxCharacteristicId,
      label: '透传'
    });
  }

  if (runtimeState.configCharacteristicId &&
      normalizeUuid(runtimeState.configCharacteristicId) !== normalizeUuid(runtimeState.rxCharacteristicId)) {
    notifyTargets.push({
      characteristicId: runtimeState.configCharacteristicId,
      label: '配置'
    });
  }

  if (notifyTargets.length === 0) {
    updateState({
      lastError: '未找到可用的 BLE 通知特征'
    });
    return;
  }

  notifyTargets.forEach((target) => {
    uni.notifyBLECharacteristicValueChange({
      deviceId: runtimeState.deviceId,
      serviceId: runtimeState.serviceId,
      characteristicId: target.characteristicId,
      state: true,
      success() {
        successCount += 1;
        finishNotifyAttempt();
      },
      fail(error) {
        errors.push(makeError(`打开 BLE ${target.label}通知失败`, error));
        finishNotifyAttempt();
      }
    });
  });
}

function enableNotifyForBridgeStrictAttempt(notifyTargets, attempt) {
  let completed = 0;
  let primaryReady = false;
  const errors = [];

  function finishNotifyAttempt() {
    completed += 1;

    if (completed < notifyTargets.length) {
      return;
    }

    if (primaryReady) {
      clearNotifyRetryTimer();
      updateState({
        notifyReady: true,
        notifyReadyAt: Date.now(),
        lastError: ''
      });
      requestPreferredMtu();
      return;
    }

    const message = errors.join('; ') || 'BLE transparent notify is unavailable';
    if (attempt < BLE_NOTIFY_MAX_ATTEMPTS) {
      updateState({
        notifyReady: false,
        notifyReadyAt: 0,
        lastError: `${message}; retry in ${BLE_NOTIFY_RETRY_DELAY_MS} ms.`
      });
      clearNotifyRetryTimer();
      notifyRetryTimer = setTimeout(() => {
        notifyRetryTimer = null;
        if (!runtimeState.clientConnected || !runtimeState.deviceId || !runtimeState.serviceId) {
          return;
        }
        enableNotifyForBridgeStrictAttempt(notifyTargets, attempt + 1);
      }, BLE_NOTIFY_RETRY_DELAY_MS);
      return;
    }

    updateState({
      notifyReady: false,
      notifyReadyAt: 0,
      lastError: message
    });
  }

  notifyTargets.forEach((target) => {
    uni.notifyBLECharacteristicValueChange({
      deviceId: runtimeState.deviceId,
      serviceId: runtimeState.serviceId,
      characteristicId: target.characteristicId,
      state: true,
      success() {
        if (target.required) {
          primaryReady = true;
        }
        finishNotifyAttempt();
      },
      fail(error) {
        errors.push(makeError(`打开 BLE ${target.label}通知失败`, error));
        finishNotifyAttempt();
      }
    });
  });
}

function enableNotifyForBridgeStrict() {
  const notifyTargets = [];

  clearNotifyRetryTimer();

  if (runtimeState.rxCharacteristicId) {
    notifyTargets.push({
      characteristicId: runtimeState.rxCharacteristicId,
      label: '透传',
      required: true
    });
  }

  if (runtimeState.configCharacteristicId &&
      normalizeUuid(runtimeState.configCharacteristicId) !== normalizeUuid(runtimeState.rxCharacteristicId)) {
    notifyTargets.push({
      characteristicId: runtimeState.configCharacteristicId,
      label: '配置',
      required: false
    });
  }

  if (notifyTargets.length === 0) {
    updateState({
      notifyReady: false,
      notifyReadyAt: 0,
      lastError: 'No usable BLE notify characteristic was found'
    });
    return;
  }

  enableNotifyForBridgeStrictAttempt(notifyTargets, 1);
}

function inspectServiceCharacteristics(services, index, deviceId, token, diagnostics, callback) {
  if (!isDiscoveryRequestActive(deviceId, token)) {
    return;
  }

  if (index >= services.length) {
    callback(null, diagnostics);
    return;
  }

  const service = services[index] || {};
  const serviceId = service.uuid;
  const normalizedServiceId = normalizeUuid(serviceId);

  uni.getBLEDeviceCharacteristics({
    deviceId,
    serviceId,
    success(result) {
      if (!isDiscoveryRequestActive(deviceId, token)) {
        return;
      }

      const characteristics = result.characteristics || [];
      let rxCharacteristicId = '';
      let txCharacteristicId = '';
      let configCharacteristicId = '';

      diagnostics.serviceSummaries.push(
        `${normalizedServiceId || serviceId}: ${summarizeCharacteristics(characteristics)}`
      );

      characteristics.forEach((item) => {
        const properties = item.properties || {};

        if (!rxCharacteristicId && uuidMatchesShortCode(item.uuid, 'FFF1') && properties.notify) {
          rxCharacteristicId = item.uuid;
        }

        if (!txCharacteristicId &&
            uuidMatchesShortCode(item.uuid, 'FFF2') &&
            (properties.write || properties.writeNoResponse)) {
          txCharacteristicId = item.uuid;
        }

        if (!configCharacteristicId && uuidMatchesShortCode(item.uuid, 'FFF3')) {
          configCharacteristicId = item.uuid;
        }
      });

      if (rxCharacteristicId && txCharacteristicId) {
        callback({
          serviceId,
          rxCharacteristicId,
          txCharacteristicId,
          configCharacteristicId
        }, diagnostics);
        return;
      }

      inspectServiceCharacteristics(services, index + 1, deviceId, token, diagnostics, callback);
    },
    fail(error) {
      if (!isDiscoveryRequestActive(deviceId, token)) {
        return;
      }

      diagnostics.characteristicErrors.push(
        `${normalizedServiceId || serviceId}: ${stringifyError(error) || '读取特征失败'}`
      );
      inspectServiceCharacteristics(services, index + 1, deviceId, token, diagnostics, callback);
    }
  });
}

function scheduleBridgeCharacteristicDiscovery(deviceId, attempt, delayMs) {
  const token = discoveryRequestToken;

  clearDiscoveryRetryTimer();
  discoveryRetryTimer = setTimeout(() => {
    discoveryRetryTimer = null;
    discoverBridgeCharacteristics(deviceId, attempt, token);
  }, Math.max(0, delayMs));
}

function discoverBridgeCharacteristics(deviceId, attempt, token) {
  if (!isDiscoveryRequestActive(deviceId, token)) {
    return;
  }

  uni.getBLEDeviceServices({
    deviceId,
    success(result) {
      if (!isDiscoveryRequestActive(deviceId, token)) {
        return;
      }

      const services = result.services || [];
      const diagnostics = {
        stage: 'characteristics',
        detail: '',
        serviceSummaries: [],
        characteristicErrors: [],
        missing: ''
      };

      inspectServiceCharacteristics(services, 0, deviceId, token, diagnostics, (matched, finalDiagnostics) => {
        if (!isDiscoveryRequestActive(deviceId, token)) {
          return;
        }

        if (matched) {
          updateState({
            serviceId: matched.serviceId,
            rxCharacteristicId: matched.rxCharacteristicId,
            txCharacteristicId: matched.txCharacteristicId,
            configCharacteristicId: matched.configCharacteristicId,
            lastError: ''
          });
          enableNotifyForBridgeStrict();
          return;
        }

        finalDiagnostics.missing = 'FFF1(notify) 或 FFF2(write/writeNoRsp)';
        const message = makeDiscoveryErrorMessage(attempt, finalDiagnostics);

        if (attempt < BLE_DISCOVERY_MAX_ATTEMPTS) {
          updateState({
            lastError: `${message}，${BLE_DISCOVERY_RETRY_DELAY_MS} ms 后重试。`
          });
          scheduleBridgeCharacteristicDiscovery(deviceId, attempt + 1, BLE_DISCOVERY_RETRY_DELAY_MS);
          return;
        }

        updateState({
          lastError: message
        });
      });
    },
    fail(error) {
      if (!isDiscoveryRequestActive(deviceId, token)) {
        return;
      }

      const diagnostics = {
        stage: 'services',
        detail: stringifyError(error)
      };
      const message = makeDiscoveryErrorMessage(attempt, diagnostics);

      if (attempt < BLE_DISCOVERY_MAX_ATTEMPTS) {
        updateState({
          lastError: `${message}，${BLE_DISCOVERY_RETRY_DELAY_MS} ms 后重试。`
        });
        scheduleBridgeCharacteristicDiscovery(deviceId, attempt + 1, BLE_DISCOVERY_RETRY_DELAY_MS);
        return;
      }

      updateState({
        lastError: message
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
  invalidateDiscoveryRequests();

  uni.createBLEConnection({
    deviceId,
    timeout: 15000,
    success() {
      connectingDeviceId = '';
      invalidateDiscoveryRequests();
      updateState({
        clientConnected: true,
        deviceId,
        deviceName,
        serviceId: '',
        rxCharacteristicId: '',
        txCharacteristicId: '',
        configCharacteristicId: '',
        notifyReady: false,
        notifyReadyAt: 0,
        lastError: ''
      });
      scheduleBridgeCharacteristicDiscovery(deviceId, 1, BLE_DISCOVERY_INITIAL_DELAY_MS);
    },
    fail(error) {
      connectingDeviceId = '';
      invalidateDiscoveryRequests();
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

  // Accept the current ATK-BLE04 naming family plus the configured fleet prefix.
  const knownFamilies = ['infinite-robot', 'ATK-BLE'];
  for (let i = 0; i < knownFamilies.length; i += 1) {
    if (name.indexOf(knownFamilies[i]) !== -1) {
      return true;
    }
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
    invalidateDiscoveryRequests();

    updateState({
      clientConnected: false,
      notifyReady: false,
      notifyReadyAt: 0,
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

    if (!isBridgeNotifyCharacteristic(result.characteristicId)) {
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
  invalidateDiscoveryRequests();
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
  invalidateDiscoveryRequests();
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

  enqueueTextLine(String(text || ''), {
    replacePendingQueue: !!(options && options.replacePendingQueue),
    priority: (options && options.priority) || 'command'
  });

  return {
    ok: true
  };
}

export function sendControlFrame(frame) {
  return sendTextLine(serializeControlFrame(frame), {
    replacePendingQueue: true,
    priority: 'control'
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
