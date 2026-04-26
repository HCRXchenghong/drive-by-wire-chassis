<template>
  <view class="page-shell">
    <view class="safe-top" :style="{ height: `${statusBarHeight}px` }"></view>

    <view class="page-narrow dashboard-shell">
      <view class="dashboard-head">
        <view class="head-copy">
          <text class="head-title">{{ currentTabTitle }}</text>
          <text class="head-subtitle">{{ connectedProfile ? connectedProfile.name : '未连接底盘' }}</text>
        </view>

        <view class="head-action" @tap="disconnectDevice">
          <AppSvgIcon name="power" :size="18" color="#F56C6C" />
        </view>
      </view>

      <scroll-view scroll-y class="dashboard-scroll">
        <view v-if="activeTab === 'profile'" class="tab-stack">
          <view class="panel-card">
            <view class="card-head">
              <text class="card-head-title">底盘档案</text>
            </view>

            <view class="panel-body">
              <view class="profile-hero">
                <view class="profile-hero-icon">
                  <AppSvgIcon name="cpu" :size="28" color="#409EFF" />
                </view>
                <text class="profile-name">{{ connectedProfile ? connectedProfile.name : '--' }}</text>
                <text class="profile-code">{{ connectedProfile ? connectedProfile.chassisCode : '--' }}</text>
              </view>

              <view class="info-list">
                <view class="info-row">
                  <text class="info-label">通信模块</text>
                  <text class="info-value">{{ connectedProfile ? connectedProfile.radioLink : 'EWM22 BLE' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">蓝牙名称</text>
                  <text class="info-value">{{ bridgeState.deviceName || (connectedProfile && connectedProfile.bleNameHint) || '--' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">控制链路</text>
                  <text class="info-value">手机 BLE Central / EWM22 BLE 透传 / STM32F407</text>
                </view>
                <view class="info-row">
                  <text class="info-label">驱动控制器</text>
                  <text class="info-value">{{ connectedProfile ? connectedProfile.driveController : 'MSSD-60EHB_2D' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">转向控制器</text>
                  <text class="info-value">{{ connectedProfile ? connectedProfile.steeringController : 'MSSC' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">底盘形式</text>
                  <text class="info-value">{{ formatChassisType(appConfig.chassisType) }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">驱动形式</text>
                  <text class="info-value">{{ formatDriveAxle(appConfig.driveAxle) }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">机械转向 CAN ID</text>
                  <text class="info-value">{{ appConfig.steerCanNodeId || 1 }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">线性方向盘 CAN ID</text>
                  <text class="info-value">{{ appConfig.handwheelCanNodeId || 2 }}</text>
                </view>
              </view>
            </view>
          </view>

          <view class="panel-card">
            <view class="card-head">
              <text class="card-head-title">结构参数</text>
            </view>

            <view class="panel-body">
              <view class="metric-grid">
                <view class="metric-card">
                  <text class="metric-label">前轮距</text>
                  <text class="metric-value">{{ appConfig.frontTrackMm }}</text>
                  <text class="metric-unit">mm</text>
                </view>
                <view class="metric-card">
                  <text class="metric-label">轴距</text>
                  <text class="metric-value">{{ appConfig.wheelbaseMm }}</text>
                  <text class="metric-unit">mm</text>
                </view>
                <view class="metric-card">
                  <text class="metric-label">后轮距</text>
                  <text class="metric-value">{{ appConfig.rearTrackMm }}</text>
                  <text class="metric-unit">mm</text>
                </view>
                <view class="metric-card">
                  <text class="metric-label">踏板模式</text>
                  <text class="metric-value metric-value-small">{{ formatPedalConfig(appConfig.pedalConfig) }}</text>
                  <text class="metric-unit">config</text>
                </view>
              </view>
            </view>
          </view>
        </view>

        <view v-else-if="activeTab === 'status'" class="tab-stack">
          <view class="panel-card">
            <view class="card-head">
              <text class="card-head-title">在线状态</text>
            </view>

            <view class="panel-body">
              <view class="status-banner">
                <view class="status-badge" :class="statusOnline ? 'badge-online' : 'badge-offline'">
                  <text class="status-badge-text">{{ statusOnline ? '在线' : '离线' }}</text>
                </view>
                <text class="status-summary">
                  {{ statusOnline ? '已收到 STM32 实时回传。' : '暂未收到 STM32 实时回传。' }}
                </text>
              </view>

              <view class="status-grid">
                <view class="status-chip" :class="bleLinkOnline ? 'chip-on' : 'chip-off'">
                  <text class="status-chip-title">BLE 链路</text>
                  <text class="status-chip-value">{{ bleLinkOnline ? '已连接' : '未连接' }}</text>
                </view>
                <view class="status-chip" :class="telemetryStatus && telemetryStatus.driveCanReady ? 'chip-on' : 'chip-off'">
                  <text class="status-chip-title">驱动 CAN</text>
                  <text class="status-chip-value">{{ telemetryStatus && telemetryStatus.driveCanReady ? '正常' : '未就绪' }}</text>
                </view>
                <view class="status-chip" :class="telemetryStatus && telemetryStatus.steerCanReady ? 'chip-on' : 'chip-off'">
                  <text class="status-chip-title">转向 CAN</text>
                  <text class="status-chip-value">{{ telemetryStatus && telemetryStatus.steerCanReady ? '正常' : '未就绪' }}</text>
                </view>
                <view class="status-chip" :class="telemetryStatus && telemetryStatus.steeringFeedbackValid ? 'chip-on' : 'chip-off'">
                  <text class="status-chip-title">机械转向反馈</text>
                  <text class="status-chip-value">{{ telemetryStatus && telemetryStatus.steeringFeedbackValid ? '实时回传' : '暂无回传' }}</text>
                </view>
                <view class="status-chip" :class="controlOwner === 'REMOTE' ? 'chip-on' : 'chip-off'">
                  <text class="status-chip-title">当前控制权</text>
                  <text class="status-chip-value">{{ controlOwner === 'REMOTE' ? '手机远程' : '本地控制' }}</text>
                </view>
                <view class="status-chip" :class="remoteMode === 'TAKEOVER' ? 'chip-on' : 'chip-off'">
                  <text class="status-chip-title">远程模式</text>
                  <text class="status-chip-value">{{ remoteMode === 'TAKEOVER' ? '接管' : '监视' }}</text>
                </view>
              </view>

              <view class="info-list info-list-tight">
                <view class="info-row">
                  <text class="info-label">本地控制正在生效</text>
                  <text class="info-value">{{ localControlActive ? '是' : '否' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">车辆运动锁</text>
                  <text class="info-value">{{ vehicleMoving ? '运动中' : '静止' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">缓停状态</text>
                  <text class="info-value">{{ softStopActive ? '已激活' : '未激活' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">急停状态</text>
                  <text class="info-value">{{ emergencyStopActive ? '已激活' : '未激活' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">硬件急停输入</text>
                  <text class="info-value">{{ hardwareEstopActive ? '已按下' : '未触发' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">机械转向 CAN ID</text>
                  <text class="info-value">{{ telemetryStatus ? telemetryStatus.steerCanNodeId : appConfig.steerCanNodeId }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">线性方向盘 CAN ID</text>
                  <text class="info-value">{{ telemetryStatus ? telemetryStatus.handwheelCanNodeId : appConfig.handwheelCanNodeId }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">线性方向盘状态</text>
                  <text class="info-value">
                    {{ linearSteeringSupported ? (linearSteeringState.detected ? '已检测到真实回包' : '协议在线，等待设备回包') : '固件未接入' }}
                  </text>
                </view>
              </view>
            </view>
          </view>

          <view class="panel-card">
            <view class="card-head">
              <text class="card-head-title">实时遥测</text>
            </view>

            <view class="panel-body">
              <view class="metric-grid">
                <view class="metric-card">
                  <text class="metric-label">左轮目标</text>
                  <text class="metric-value">{{ telemetryStatus ? telemetryStatus.leftTargetRpm : 0 }}</text>
                  <text class="metric-unit">rpm</text>
                </view>
                <view class="metric-card">
                  <text class="metric-label">右轮目标</text>
                  <text class="metric-value">{{ telemetryStatus ? telemetryStatus.rightTargetRpm : 0 }}</text>
                  <text class="metric-unit">rpm</text>
                </view>
                <view class="metric-card">
                  <text class="metric-label">转向目标</text>
                  <text class="metric-value">{{ telemetryStatus ? telemetryStatus.steerTargetRpm : 0 }}</text>
                  <text class="metric-unit">rpm</text>
                </view>
                <view class="metric-card">
                  <text class="metric-label">油门 ADC</text>
                  <text class="metric-value">{{ telemetryStatus ? telemetryStatus.throttleRaw : 0 }}</text>
                  <text class="metric-unit">raw</text>
                </view>
                <view class="metric-card">
                  <text class="metric-label">刹车 ADC</text>
                  <text class="metric-value">{{ telemetryStatus ? telemetryStatus.brakeRaw : 0 }}</text>
                  <text class="metric-unit">raw</text>
                </view>
                <view class="metric-card">
                  <text class="metric-label">远程挡位</text>
                  <text class="metric-value">{{ telemetryStatus ? telemetryStatus.remoteGear : 'N' }}</text>
                  <text class="metric-unit">gear</text>
                </view>
              </view>

              <view class="info-list info-list-tight">
                <view class="info-row">
                  <text class="info-label">远程源</text>
                  <text class="info-value">{{ telemetryStatus ? telemetryStatus.remoteSource : '--' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">远程命令有效</text>
                  <text class="info-value">{{ telemetryStatus && telemetryStatus.remoteValid ? '是' : '否' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">本地挡位输入</text>
                  <text class="info-value">{{ telemetryStatus ? telemetryStatus.localGear : '--' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">转向反馈</text>
                  <text class="info-value">{{ telemetryStatus && telemetryStatus.steeringFeedbackValid ? telemetryStatus.steeringFeedback : '--' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">反馈时延</text>
                  <text class="info-value">{{ telemetryStatus && telemetryStatus.steeringFeedbackValid ? `${telemetryStatus.steeringFeedbackAgeMs} ms` : '--' }}</text>
                </view>
              </view>
            </view>
          </view>
        </view>

        <view v-else-if="activeTab === 'control'" class="tab-stack">
          <view class="panel-card">
            <view class="card-head">
              <text class="card-head-title">控制权状态</text>
            </view>

            <view class="panel-body">
              <view class="authority-row">
                <view class="authority-pill" :class="controlOwner === 'REMOTE' ? 'pill-blue' : 'pill-gray'">
                  <text class="authority-pill-text">{{ controlOwner === 'REMOTE' ? '手机已接管' : '本地优先' }}</text>
                </view>
                <view class="authority-pill" :class="remoteMode === 'TAKEOVER' ? 'pill-green' : 'pill-gray'">
                  <text class="authority-pill-text">{{ remoteMode === 'TAKEOVER' ? '远程接管模式' : '远程监视模式' }}</text>
                </view>
              </view>

              <view class="info-list info-list-tight">
                <view class="info-row">
                  <text class="info-label">本地控制状态</text>
                  <text class="info-value">{{ localControlActive ? '本地输入正在控制车辆' : '当前没有本地控制输入' }}</text>
                </view>
                <view class="info-row">
                  <text class="info-label">停止优先级</text>
                  <text class="info-value">硬件急停 / 手机急停 / 手机缓停 始终高于本地与远程驱动命令</text>
                </view>
              </view>
            </view>
          </view>

          <view class="panel-card">
            <view class="card-head">
              <text class="card-head-title">线性控制</text>
            </view>

            <view class="panel-body">
              <view class="gear-panel">
                <view class="gear-copy">
                  <text class="gear-copy-title">挡位滑动切换</text>
                  <text class="gear-copy-desc">D 只前进并缓停，S 松手滑行，R 只倒退，N 为空挡。</text>
                </view>

                <view
                  class="gear-track"
                  @tap="onGearTrackTap"
                  @touchstart.stop.prevent="onGearTrackTouch"
                  @touchmove.stop.prevent="onGearTrackTouch"
                >
                  <view class="gear-track-bg"></view>
                  <view class="gear-knob" :style="{ left: `${gearKnobLeft}px` }"></view>
                  <view
                    v-for="item in gearOptions"
                    :key="item"
                    class="gear-slot"
                    :class="{ active: currentGear === item }"
                  >
                    <text class="gear-slot-text">{{ item }}</text>
                  </view>
                </view>
              </view>

              <view class="control-metrics">
                <view class="control-metric">
                  <text class="control-metric-label">转向 X</text>
                  <text class="control-metric-value">{{ controlOutput.x }}</text>
                </view>
                <view class="control-metric">
                  <text class="control-metric-label">动力 Y</text>
                  <text class="control-metric-value">{{ controlOutput.y }}</text>
                </view>
                <view class="control-metric">
                  <text class="control-metric-label">当前挡位</text>
                  <text class="control-metric-value">{{ currentGear }}</text>
                </view>
              </view>
            </view>
          </view>

          <view class="panel-card control-pad-card">
            <view class="control-pad-wrap">
              <view
                class="joystick-base"
                :class="{ 'joystick-disabled': joystickLocked }"
                @touchstart.stop.prevent="onJoystickStart"
                @touchmove.stop.prevent="onJoystickMove"
                @touchend.stop.prevent="onJoystickEnd"
                @touchcancel.stop.prevent="onJoystickEnd"
                @mousedown.stop.prevent="onJoystickStart"
                @mousemove.stop.prevent="onJoystickMove"
                @mouseup.stop.prevent="onJoystickEnd"
                @mouseleave.stop.prevent="onJoystickEnd"
              >
                <view class="joystick-cross joystick-cross-x"></view>
                <view class="joystick-cross joystick-cross-y"></view>

                <view
                  class="joystick-knob"
                  :class="{ 'joystick-knob-neutral': currentGear === 'N' || joystickLocked }"
                  :style="{ transform: `translate(${knobPosition.x}px, ${knobPosition.y}px)` }"
                >
                  <view class="joystick-knob-core"></view>
                </view>
              </view>

              <view v-if="joystickLocked" class="estop-mask">
                <AppSvgIcon name="shield" :size="26" color="#F56C6C" />
                <text class="estop-mask-text">{{ controlLockText }}</text>
              </view>
            </view>
          </view>

          <view class="stop-row">
            <view
              class="stop-button stop-button-orange"
              :class="{ 'stop-button-active': softStopActive }"
              @tap="toggleSoftStop"
            >
              <text class="stop-button-text">{{ softStopActive ? '解除缓停' : '缓停' }}</text>
            </view>
            <view
              class="stop-button stop-button-red"
              :class="{ 'stop-button-active': emergencyStopActive || hardwareEstopActive }"
              @tap="toggleEmergencyStop"
            >
              <text class="stop-button-text">{{ emergencyStopActive ? '解除急停' : '急停' }}</text>
            </view>
          </view>
        </view>

        <view v-else class="tab-stack">
          <view class="panel-card">
            <view class="card-head">
              <text class="card-head-title">参数配置</text>
            </view>

            <view class="panel-body form-stack">
              <view class="status-inline" :class="vehicleMoving ? 'status-inline-warn' : 'status-inline-normal'">
                <text class="status-inline-text">
                  {{ vehicleMoving ? '车辆正在运动，参数保存与线性矫正已锁定。' : '车辆静止，可保存配置并进入线性矫正。' }}
                </text>
              </view>

              <view class="form-group">
                <text class="form-label">底盘形式</text>
                <view class="choice-row">
                  <view
                    class="choice-chip"
                    :class="{ active: draftConfig.chassisType === 'ackermann' }"
                    @tap="draftConfig.chassisType = 'ackermann'"
                  >
                    <text class="choice-chip-text">线性阿克曼</text>
                  </view>
                  <view
                    class="choice-chip"
                    :class="{ active: draftConfig.chassisType === 'diff' }"
                    @tap="draftConfig.chassisType = 'diff'"
                  >
                    <text class="choice-chip-text">线性差速</text>
                  </view>
                </view>
              </view>

              <view class="form-group">
                <text class="form-label">驱动形式</text>
                <view class="choice-row">
                  <view
                    class="choice-chip"
                    :class="{ active: draftConfig.driveAxle === 'fwd' }"
                    @tap="draftConfig.driveAxle = 'fwd'"
                  >
                    <text class="choice-chip-text">前驱</text>
                  </view>
                  <view
                    class="choice-chip"
                    :class="{ active: draftConfig.driveAxle === 'rwd' }"
                    @tap="draftConfig.driveAxle = 'rwd'"
                  >
                    <text class="choice-chip-text">后驱</text>
                  </view>
                </view>
              </view>

              <view class="form-group">
                <text class="form-label">前轮距 (mm)</text>
                <input v-model="draftConfig.frontTrackMm" class="number-input" type="number" />
              </view>

              <view class="form-group">
                <text class="form-label">轴距 (mm)</text>
                <input v-model="draftConfig.wheelbaseMm" class="number-input" type="number" />
              </view>

              <view class="form-group">
                <text class="form-label">后轮距 (mm)</text>
                <input v-model="draftConfig.rearTrackMm" class="number-input" type="number" />
              </view>

              <view class="form-group">
                <text class="form-label">机械转向 CAN 节点号</text>
                <input v-model="draftConfig.steerCanNodeId" class="number-input" type="number" />
              </view>

              <view class="form-group">
                <text class="form-label">线性方向盘 CAN 节点号</text>
                <input v-model="draftConfig.handwheelCanNodeId" class="number-input" type="number" />
              </view>

              <view class="switch-row">
                <view class="switch-copy">
                  <text class="switch-title">线性方向盘</text>
                  <text class="switch-desc">
                    {{ linearSteeringSupported ? (linearSteeringState.detected ? '已检测到线性方向盘真实 CAN 回包。' : '协议已接入，但当前尚未收到线性方向盘 CAN 回包。') : '当前固件未提供线性方向盘协议能力。' }}
                  </text>
                </view>

                <view
                  class="switch-shell"
                  :class="{ active: draftConfig.hasLinearSteering && linearSteeringSupported, disabled: !linearSteeringSupported }"
                  @tap="toggleLinearSteering"
                >
                  <view class="switch-knob"></view>
                </view>
              </view>

              <view class="button-row button-row-stack">
                <view class="ghost-button wide-button" @tap="openCalibrationPage">
                  <text class="ghost-button-text ghost-button-text-blue">进入线性矫正</text>
                </view>
                <view class="primary-button wide-button" @tap="saveConfig">
                  <text class="primary-button-text">{{ savingConfig ? '保存中...' : '保存并下发配置' }}</text>
                </view>
              </view>
            </view>
          </view>
        </view>
      </scroll-view>
    </view>

    <view class="bottom-nav">
      <view class="nav-item" :class="{ active: activeTab === 'profile' }" @tap="switchTab('profile')">
        <AppSvgIcon name="cpu" :size="20" :color="activeTab === 'profile' ? '#409EFF' : '#909399'" />
        <text class="nav-item-text">档案</text>
      </view>
      <view class="nav-item" :class="{ active: activeTab === 'status' }" @tap="switchTab('status')">
        <AppSvgIcon name="signal" :size="20" :color="activeTab === 'status' ? '#409EFF' : '#909399'" />
        <text class="nav-item-text">状态</text>
      </view>
      <view class="nav-item" :class="{ active: activeTab === 'control' }" @tap="switchTab('control')">
        <AppSvgIcon name="bluetooth" :size="20" :color="activeTab === 'control' ? '#409EFF' : '#909399'" />
        <text class="nav-item-text">控制</text>
      </view>
      <view class="nav-item" :class="{ active: activeTab === 'config' }" @tap="switchTab('config')">
        <AppSvgIcon name="check" :size="20" :color="activeTab === 'config' ? '#409EFF' : '#909399'" />
        <text class="nav-item-text">配置</text>
      </view>
    </view>
  </view>
</template>

<script>
import AppSvgIcon from '@/components/AppSvgIcon.vue';
import {
  createControlFrame,
  createGetCalibrationCommand,
  createGetCapabilitiesCommand,
  createGetConfigCommand,
  createGetStatusCommand,
  createQueryLinearSteeringCommand,
  createSaveConfigCommand,
  createSetConfigCommand,
  createSetRemoteModeCommand,
  createSetRemoteStopStateCommand
} from '@/utils/chassis-protocol';
import { syncSessionFromBridgeState } from '@/utils/bridge-session-sync';
import {
  getAppConfig,
  getCapabilities,
  getConnectedProfile,
  getLinearSteeringState,
  getSessionState,
  persistConnectedProfile,
  updateAppConfig
} from '@/utils/session-state';
import { getBridgeState, sendControlFrame, sendJsonCommand, stopBleSession } from '@/utils/ewm22-ble-bridge';
import { getLatestStatusSnapshot, getLatestStatusTimestamp } from '@/utils/bridge-status';

const JOYSTICK_RADIUS = 82;
const GEAR_TRACK_WIDTH = 204;
const GEAR_KNOB_WIDTH = 62;

function clamp(value, minValue, maxValue) {
  if (value < minValue) {
    return minValue;
  }

  if (value > maxValue) {
    return maxValue;
  }

  return value;
}

function modalPromise(options) {
  return new Promise((resolve) => {
    uni.showModal({
      ...options,
      success(result) {
        resolve(result);
      },
      fail() {
        resolve({
          confirm: false,
          cancel: true
        });
      }
    });
  });
}

export default {
  components: {
    AppSvgIcon
  },
  data() {
    return {
      statusBarHeight: 0,
      activeTab: 'control',
      bridgeState: getBridgeState(),
      sessionState: getSessionState(),
      connectedProfile: getConnectedProfile(),
      syncTicker: null,
      controlTicker: null,
      controlSeq: 1,
      currentGear: 'N',
      gearOptions: ['D', 'S', 'N', 'R'],
      gearTrackRect: null,
      joystickRect: null,
      isDragging: false,
      controlOutput: {
        x: 0,
        y: 0
      },
      commandOutput: {
        x: 0,
        y: 0
      },
      knobPosition: {
        x: 0,
        y: 0
      },
      releaseRampActive: false,
      authorityPromptBusy: false,
      authorityPromptShown: false,
      takeoverRequestBusy: false,
      stopCommandBusy: false,
      remoteCommandLock: false,
      savingConfig: false,
      lastStatusQueryAt: 0,
      lastMetaQueryAt: 0,
      draftConfig: {
        chassisType: 'ackermann',
        driveAxle: 'rwd',
        frontTrackMm: '1280',
        wheelbaseMm: '1720',
        rearTrackMm: '1260',
        steerCanNodeId: '1',
        handwheelCanNodeId: '2',
        hasLinearSteering: false,
        pedalConfig: 'brake_throttle'
      }
    };
  },
  computed: {
    appConfig() {
      return this.sessionState.appConfig || getAppConfig();
    },
    capabilities() {
      return this.sessionState.capabilities || getCapabilities() || {};
    },
    linearSteeringSupported() {
      return !!this.capabilities.linearSteeringSupported;
    },
    linearSteeringState() {
      return this.sessionState.linearSteeringState || getLinearSteeringState();
    },
    latestStatus() {
      return getLatestStatusSnapshot(this.bridgeState);
    },
    telemetryStatus() {
      return this.latestStatus;
    },
    statusOnline() {
      const lastAt = getLatestStatusTimestamp(this.bridgeState);
      return !!lastAt && (Date.now() - lastAt) < 1600;
    },
    bleLinkOnline() {
      return !!this.bridgeState.clientConnected;
    },
    controlOwner() {
      return String((this.telemetryStatus && this.telemetryStatus.controlOwner) || 'LOCAL').toUpperCase();
    },
    remoteMode() {
      return String((this.telemetryStatus && this.telemetryStatus.remoteMode) || 'MONITOR').toUpperCase();
    },
    localControlActive() {
      return !!(this.telemetryStatus && this.telemetryStatus.localControlActive);
    },
    vehicleMoving() {
      return !!(this.telemetryStatus && this.telemetryStatus.vehicleMoving);
    },
    softStopActive() {
      return !!(this.telemetryStatus && this.telemetryStatus.softStopActive);
    },
    emergencyStopActive() {
      return !!(this.telemetryStatus && this.telemetryStatus.emergencyStopActive);
    },
    hardwareEstopActive() {
      return !!(this.telemetryStatus && this.telemetryStatus.hardwareEstopActive);
    },
    joystickLocked() {
      return this.softStopActive || this.emergencyStopActive || this.hardwareEstopActive;
    },
    controlLockText() {
      if (this.hardwareEstopActive) {
        return '硬件急停已触发';
      }

      if (this.emergencyStopActive) {
        return '系统已急停锁定';
      }

      if (this.softStopActive) {
        return '系统已进入缓停';
      }

      return '';
    },
    currentTabTitle() {
      if (this.activeTab === 'profile') {
        return '档案信息';
      }

      if (this.activeTab === 'status') {
        return '状态概览';
      }

      if (this.activeTab === 'config') {
        return '参数配置';
      }

      return '线性控制';
    },
    gearKnobLeft() {
      const index = Math.max(0, this.gearOptions.indexOf(this.currentGear));
      const travel = GEAR_TRACK_WIDTH - GEAR_KNOB_WIDTH - 8;
      const slotCount = Math.max(1, this.gearOptions.length - 1);
      return 4 + Math.round((travel * index) / slotCount);
    }
  },
  onLoad() {
    try {
      const systemInfo = uni.getSystemInfoSync();
      this.statusBarHeight = Number(systemInfo.statusBarHeight || 0);
    } catch (error) {
      this.statusBarHeight = 0;
    }

    this.connectedProfile = getConnectedProfile();
    if (!this.connectedProfile) {
      uni.reLaunch({
        url: '/pages/index/index'
      });
      return;
    }

    this.sessionState = getSessionState();
    this.syncDraftConfig();
    this.startSyncTicker();
    this.startControlTicker();

    this.$nextTick(() => {
      this.refreshLayoutRects();
    });
  },
  onShow() {
    this.connectedProfile = getConnectedProfile();
    this.sessionState = syncSessionFromBridgeState(getBridgeState());
    this.syncDraftConfig();
    this.requestSnapshot();
    this.$nextTick(() => {
      this.refreshLayoutRects();
    });
  },
  onUnload() {
    clearInterval(this.syncTicker);
    clearInterval(this.controlTicker);
    this.syncTicker = null;
    this.controlTicker = null;
    this.resetJoystick(false);
    this.sendNeutralFrame();
  },
  methods: {
    refreshLayoutRects() {
      const query = uni.createSelectorQuery().in(this);
      query.select('.joystick-base').boundingClientRect();
      query.select('.gear-track').boundingClientRect();
      query.exec((result) => {
        this.joystickRect = result && result[0] ? result[0] : null;
        this.gearTrackRect = result && result[1] ? result[1] : null;
      });
    },
    switchTab(tab) {
      this.activeTab = tab;
      if (tab !== 'control') {
        this.resetJoystick(false);
      }

      this.$nextTick(() => {
        this.refreshLayoutRects();
      });
    },
    startSyncTicker() {
      clearInterval(this.syncTicker);
      this.syncTicker = setInterval(() => {
        this.bridgeState = getBridgeState();
        this.connectedProfile = getConnectedProfile();
        this.sessionState = syncSessionFromBridgeState(this.bridgeState);

        const now = Date.now();
        if (this.bridgeState.clientConnected && (now - this.lastStatusQueryAt) > 1000) {
          this.lastStatusQueryAt = now;
          sendJsonCommand(createGetStatusCommand());
        }

        if (this.bridgeState.clientConnected && (now - this.lastMetaQueryAt) > 2200) {
          this.lastMetaQueryAt = now;
          sendJsonCommand(createGetCapabilitiesCommand());
          sendJsonCommand(createGetConfigCommand());
          sendJsonCommand(createQueryLinearSteeringCommand());
        }

        if (!this.localControlActive || this.remoteMode === 'TAKEOVER') {
          this.authorityPromptShown = false;
        }

        this.maybePromptAuthorityChoice();
      }, 180);
    },
    startControlTicker() {
      clearInterval(this.controlTicker);
      this.controlTicker = setInterval(() => {
        if (this.activeTab !== 'control') {
          return;
        }

        this.tickReleaseRamp();
        this.sendCurrentControlFrame();
      }, 90);
    },
    requestSnapshot() {
      sendJsonCommand(createGetCapabilitiesCommand());
      sendJsonCommand(createGetConfigCommand());
      sendJsonCommand(createGetStatusCommand());
      sendJsonCommand(createGetCalibrationCommand());
      sendJsonCommand(createQueryLinearSteeringCommand());
    },
    syncDraftConfig() {
      const config = this.appConfig || getAppConfig();
      this.draftConfig = {
        chassisType: config.chassisType || 'ackermann',
        driveAxle: config.driveAxle || 'rwd',
        frontTrackMm: String(config.frontTrackMm || 1280),
        wheelbaseMm: String(config.wheelbaseMm || 1720),
        rearTrackMm: String(config.rearTrackMm || 1260),
        steerCanNodeId: String(config.steerCanNodeId || 1),
        handwheelCanNodeId: String(config.handwheelCanNodeId || 2),
        hasLinearSteering: !!config.hasLinearSteering,
        pedalConfig: config.pedalConfig || 'brake_throttle'
      };
    },
    buildDraftConfigPayload() {
      return {
        chassisType: this.draftConfig.chassisType === 'diff' ? 'diff' : 'ackermann',
        driveAxle: this.draftConfig.driveAxle === 'fwd' ? 'fwd' : 'rwd',
        frontTrackMm: clamp(Number(this.draftConfig.frontTrackMm || 0), 100, 10000),
        wheelbaseMm: clamp(Number(this.draftConfig.wheelbaseMm || 0), 100, 10000),
        rearTrackMm: clamp(Number(this.draftConfig.rearTrackMm || 0), 100, 10000),
        steerCanNodeId: clamp(Number(this.draftConfig.steerCanNodeId || 0), 1, 0x7FF),
        handwheelCanNodeId: clamp(Number(this.draftConfig.handwheelCanNodeId || 0), 1, 0x7FF),
        hasLinearSteering: !!this.draftConfig.hasLinearSteering && this.linearSteeringSupported,
        pedalConfig: this.draftConfig.pedalConfig || 'brake_throttle'
      };
    },
    waitForCondition(checker, timeoutMs) {
      const startAt = Date.now();

      return new Promise((resolve) => {
        const timer = setInterval(() => {
          this.bridgeState = getBridgeState();
          this.sessionState = syncSessionFromBridgeState(this.bridgeState);

          if (checker(this.bridgeState, this.sessionState)) {
            clearInterval(timer);
            resolve(true);
            return;
          }

          if ((Date.now() - startAt) >= timeoutMs) {
            clearInterval(timer);
            resolve(false);
          }
        }, 100);
      });
    },
    showVehicleMovingModal() {
      uni.showModal({
        title: '车辆正在运动',
        content: '请停车后再试。',
        showCancel: false
      });
    },
    ensureVehicleStopped() {
      if (this.vehicleMoving) {
        this.showVehicleMovingModal();
        return false;
      }

      return true;
    },
    maybePromptAuthorityChoice() {
      if (!this.bridgeState.clientConnected || !this.telemetryStatus) {
        return;
      }

      if (!this.localControlActive || this.remoteMode === 'TAKEOVER') {
        return;
      }

      if (this.authorityPromptBusy || this.authorityPromptShown) {
        return;
      }

      this.authorityPromptBusy = true;
      this.authorityPromptShown = true;

      uni.showModal({
        title: '检测到本地控制',
        content: '本地踏板或挡位正在控制车辆。你可以接管车辆，也可以只保持监视控制。',
        confirmText: '接管车辆',
        cancelText: '监视控制',
        success: async (result) => {
          if (result.confirm) {
            await this.requestRemoteMode('TAKEOVER');
          } else {
            await this.requestRemoteMode('MONITOR');
          }
        },
        complete: () => {
          this.authorityPromptBusy = false;
        }
      });
    },
    async requestRemoteMode(mode) {
      if (!this.bridgeState.clientConnected || !this.bridgeState.notifyReady) {
        uni.showToast({
          title: '蓝牙链路未就绪',
          icon: 'none'
        });
        return false;
      }

      const targetMode = String(mode || 'MONITOR').toUpperCase();
      const requestAt = Date.now();
      this.remoteCommandLock = true;

      sendJsonCommand(createSetRemoteModeCommand(targetMode), {
        replacePendingQueue: true
      });

      const ok = await this.waitForCondition((bridgeState) => {
        const latestStatus = getLatestStatusSnapshot(bridgeState);
        return !!latestStatus &&
          !!bridgeState.lastJsonAt &&
          bridgeState.lastJsonAt >= requestAt &&
          String(latestStatus.remoteMode || '').toUpperCase() === targetMode;
      }, 1800);
      this.remoteCommandLock = false;

      if (!ok) {
        uni.showToast({
          title: '远程模式切换失败',
          icon: 'none'
        });
        return false;
      }

      return true;
    },
    async ensureDriveAuthority() {
      if (!this.bridgeState.clientConnected || !this.bridgeState.notifyReady) {
        uni.showToast({
          title: '蓝牙链路未就绪',
          icon: 'none'
        });
        return false;
      }

      if (!this.telemetryStatus) {
        this.requestSnapshot();
        uni.showToast({
          title: '等待 STM32 状态同步',
          icon: 'none'
        });
        return false;
      }

      if (this.remoteMode === 'TAKEOVER') {
        return true;
      }

      if (this.takeoverRequestBusy) {
        return false;
      }

      this.takeoverRequestBusy = true;
      const result = await modalPromise({
        title: '申请远程接管',
        content: this.localControlActive
          ? '当前本地控制正在生效。确认后手机将立即接管车辆，本地驱动输入失效，急停除外。'
          : '当前为监视控制。确认后手机将接管车辆。',
        confirmText: '接管车辆',
        cancelText: '继续监视'
      });

      let granted = false;
      if (result.confirm) {
        granted = await this.requestRemoteMode('TAKEOVER');
      } else {
        await this.requestRemoteMode('MONITOR');
      }

      this.takeoverRequestBusy = false;
      return granted;
    },
    async requestStopState(options) {
      if (!this.bridgeState.clientConnected || !this.bridgeState.notifyReady || this.stopCommandBusy) {
        if (!this.stopCommandBusy) {
          uni.showToast({
            title: '蓝牙链路未就绪',
            icon: 'none'
          });
        }
        return false;
      }

      this.stopCommandBusy = true;
      const targetSoftStop = !!(options && options.softStop);
      const targetEstop = !!(options && options.estop);
      const requestAt = Date.now();
      this.remoteCommandLock = true;

      sendJsonCommand(createSetRemoteStopStateCommand({
        softStop: targetSoftStop,
        estop: targetEstop
      }), {
        replacePendingQueue: true
      });

      const ok = await this.waitForCondition((bridgeState) => {
        const latestStatus = getLatestStatusSnapshot(bridgeState);
        if (!latestStatus || !bridgeState.lastJsonAt || bridgeState.lastJsonAt < requestAt) {
          return false;
        }

        return !!latestStatus &&
          !!latestStatus.softStopActive === targetSoftStop &&
          !!latestStatus.emergencyStopActive === targetEstop;
      }, 1800);
      this.remoteCommandLock = false;

      this.stopCommandBusy = false;

      if (!ok) {
        uni.showToast({
          title: '停止命令下发失败',
          icon: 'none'
        });
        return false;
      }

      this.resetJoystick(false);
      if (targetEstop) {
        this.currentGear = 'N';
      }
      return true;
    },
    async saveConfig() {
      if (this.savingConfig) {
        return;
      }

      if (!this.ensureVehicleStopped()) {
        return;
      }

      const payload = this.buildDraftConfigPayload();
      this.savingConfig = true;

      sendJsonCommand(createSetConfigCommand(payload), {
        replacePendingQueue: true
      });
      sendJsonCommand(createGetConfigCommand());

      const requestAt = Date.now();
      sendJsonCommand(createSaveConfigCommand());

      const ok = await this.waitForCondition((bridgeState) => {
        return !!bridgeState.saveResultAt &&
          bridgeState.saveResultAt >= requestAt &&
          bridgeState.saveResult &&
          bridgeState.saveResult.domain === 'config';
      }, 2200);

      this.savingConfig = false;

      if (!ok) {
        uni.showToast({
          title: '配置保存超时',
          icon: 'none'
        });
        return;
      }

      if (this.bridgeState.saveResult && this.bridgeState.saveResult.ok) {
        updateAppConfig(payload);
        persistConnectedProfile(this.bridgeState);
        this.requestSnapshot();
        uni.showToast({
          title: '配置已下发',
          icon: 'success'
        });
        return;
      }

      if (this.bridgeState.saveResult && this.bridgeState.saveResult.error === 'vehicle_moving') {
        this.showVehicleMovingModal();
        return;
      }

      uni.showToast({
        title: '配置保存失败',
        icon: 'none'
      });
    },
    async toggleLinearSteering() {
      if (!this.ensureVehicleStopped()) {
        return;
      }

      if (!this.linearSteeringSupported) {
        this.draftConfig.hasLinearSteering = false;
        uni.showModal({
          title: '线性方向盘协议未接入',
          content: '当前固件未提供线性方向盘协议能力，暂时不能开启。',
          showCancel: false
        });
        return;
      }

      const nextValue = !this.draftConfig.hasLinearSteering;
      this.draftConfig.hasLinearSteering = nextValue;

      if (!nextValue) {
        return;
      }

      const queryAt = Date.now();
      sendJsonCommand(createQueryLinearSteeringCommand(), {
        replacePendingQueue: true
      });

      const haveFeedback = await this.waitForCondition((bridgeState) => {
        return !!bridgeState.linearSteeringAt && bridgeState.linearSteeringAt >= queryAt;
      }, 1200);

      if (!haveFeedback) {
        this.draftConfig.hasLinearSteering = false;
        uni.showModal({
          title: '线性方向盘未响应',
          content: 'STM32 暂未收到线性方向盘 CAN 回包，请确认线性方向盘已安装、已供电并已接入转向 CAN。',
          showCancel: false
        });
        return;
      }

      const state = this.bridgeState.linearSteering || {};
      if (!state.detected) {
        const result = await modalPromise({
          title: '线性方向盘未检测到',
          content: '当前尚未收到线性方向盘真实 CAN 回包。请确认安装完成后再开启；若仅做预配置，也可以继续保持开启。',
          confirmText: '继续开启',
          cancelText: '保持关闭'
        });

        if (!result.confirm) {
          this.draftConfig.hasLinearSteering = false;
        }
      }
    },
    openCalibrationPage() {
      if (!this.ensureVehicleStopped()) {
        return;
      }

      uni.navigateTo({
        url: '/pages/calibration/index'
      });
    },
    onGearTrackTap(event) {
      this.refreshLayoutRects();
      this.applyGearFromEvent(event);
    },
    onGearTrackTouch(event) {
      this.applyGearFromEvent(event);
    },
    applyGearFromEvent(event) {
      if (!this.gearTrackRect) {
        return;
      }

      const point = this.pickPoint(event);
      if (!point) {
        return;
      }

      const width = this.gearTrackRect.width || GEAR_TRACK_WIDTH;
      const relativeX = clamp(point.x - this.gearTrackRect.left, 0, width);
      const ratio = width > 0 ? relativeX / width : 0;
      const slotCount = Math.max(1, this.gearOptions.length - 1);
      const index = clamp(Math.round(ratio * slotCount), 0, slotCount);

      this.currentGear = this.gearOptions[index];
      this.releaseRampActive = false;
      this.commandOutput.x = 0;

      if (this.currentGear === 'N' || this.currentGear === 'S') {
        this.controlOutput.y = 0;
        this.knobPosition.y = 0;
        this.commandOutput.y = 0;
      }
    },
    async onJoystickStart(event) {
      if (this.joystickLocked || this.activeTab !== 'control') {
        return;
      }

      const allowed = await this.ensureDriveAuthority();
      if (!allowed) {
        return;
      }

      this.isDragging = true;
      this.refreshLayoutRects();
      this.applyJoystickMove(event);
    },
    onJoystickMove(event) {
      if (!this.isDragging || this.joystickLocked || this.activeTab !== 'control') {
        return;
      }

      this.applyJoystickMove(event);
    },
    onJoystickEnd() {
      this.isDragging = false;
      this.resetJoystick();
    },
    applyJoystickMove(event) {
      if (!this.joystickRect) {
        return;
      }

      const point = this.pickPoint(event);
      if (!point) {
        return;
      }

      const centerX = this.joystickRect.left + (this.joystickRect.width / 2);
      const centerY = this.joystickRect.top + (this.joystickRect.height / 2);
      let dx = point.x - centerX;
      let dy = point.y - centerY;
      const distance = Math.sqrt((dx * dx) + (dy * dy));

      if (distance > JOYSTICK_RADIUS) {
        dx = (dx / distance) * JOYSTICK_RADIUS;
        dy = (dy / distance) * JOYSTICK_RADIUS;
      }

      if (this.currentGear === 'N') {
        dy = 0;
      } else if ((this.currentGear === 'D' || this.currentGear === 'S') && dy > 0) {
        dy = 0;
      } else if (this.currentGear === 'R' && dy < 0) {
        dy = 0;
      }

      const nextOutput = {
        x: clamp(Math.round((dx / JOYSTICK_RADIUS) * 100), -100, 100),
        y: clamp(Math.round((-dy / JOYSTICK_RADIUS) * 100), -100, 100)
      };

      this.knobPosition = {
        x: Math.round(dx),
        y: Math.round(dy)
      };
      this.controlOutput = nextOutput;
      this.commandOutput = {
        ...nextOutput
      };
      this.releaseRampActive = false;
    },
    resetJoystick(allowRamp = true) {
      this.controlOutput = {
        x: 0,
        y: 0
      };
      this.knobPosition = {
        x: 0,
        y: 0
      };

      this.commandOutput.x = 0;

      if (!allowRamp) {
        this.commandOutput.y = 0;
        this.releaseRampActive = false;
        return;
      }

      if (this.joystickLocked || this.currentGear === 'N' || this.currentGear === 'S') {
        this.commandOutput.y = 0;
        this.releaseRampActive = false;
        return;
      }

      if (this.currentGear === 'D' || this.currentGear === 'R') {
        this.releaseRampActive = Math.abs(this.commandOutput.y) > 0;
        return;
      }

      this.commandOutput.y = 0;
      this.releaseRampActive = false;
    },
    tickReleaseRamp() {
      const step = 12;

      if (!this.releaseRampActive || this.isDragging || this.joystickLocked) {
        return;
      }

      if (Math.abs(this.commandOutput.y) <= step) {
        this.commandOutput.y = 0;
        this.releaseRampActive = false;
        return;
      }

      this.commandOutput.y += this.commandOutput.y > 0 ? -step : step;
    },
    pickPoint(event) {
      const sourceEvent = event && event.touches && event.touches.length
        ? event.touches[0]
        : event && event.changedTouches && event.changedTouches.length
          ? event.changedTouches[0]
          : event;

      if (!sourceEvent) {
        return null;
      }

      const x = typeof sourceEvent.clientX === 'number' ? sourceEvent.clientX : sourceEvent.x;
      const y = typeof sourceEvent.clientY === 'number' ? sourceEvent.clientY : sourceEvent.y;

      if (typeof x !== 'number' || typeof y !== 'number') {
        return null;
      }

      return {
        x,
        y
      };
    },
    sendCurrentControlFrame() {
      if (!this.bridgeState.clientConnected || !this.bridgeState.notifyReady) {
        return;
      }

      if (this.remoteCommandLock) {
        return;
      }

      if (this.remoteMode !== 'TAKEOVER') {
        return;
      }

      const frame = createControlFrame({
        seq: this.controlSeq,
        gear: this.currentGear,
        throttlePercent: this.commandOutput.y,
        steerPercent: this.commandOutput.x,
        auxXPercent: 0,
        auxYPercent: 0,
        buttons: 0
      });

      const result = sendControlFrame(frame);
      if (result.ok) {
        this.controlSeq += 1;
      }
    },
    sendNeutralFrame() {
      if (!this.bridgeState.clientConnected || !this.bridgeState.notifyReady) {
        return;
      }

      const frame = createControlFrame({
        seq: this.controlSeq,
        gear: 'N',
        throttlePercent: 0,
        steerPercent: 0,
        auxXPercent: 0,
        auxYPercent: 0,
        buttons: 0
      });

      const result = sendControlFrame(frame);
      if (result.ok) {
        this.controlSeq += 1;
      }
    },
    async toggleSoftStop() {
      if (this.hardwareEstopActive) {
        uni.showToast({
          title: '硬件急停已触发',
          icon: 'none'
        });
        return;
      }

      if (this.emergencyStopActive) {
        uni.showToast({
          title: '请先解除急停',
          icon: 'none'
        });
        return;
      }

      await this.requestStopState({
        softStop: !this.softStopActive,
        estop: false
      });
    },
    async toggleEmergencyStop() {
      if (this.hardwareEstopActive && !this.emergencyStopActive) {
        uni.showToast({
          title: '硬件急停已触发',
          icon: 'none'
        });
        return;
      }

      await this.requestStopState({
        softStop: false,
        estop: !this.emergencyStopActive
      });
    },
    async disconnectDevice() {
      this.isDragging = false;
      this.resetJoystick(false);
      this.currentGear = 'N';
      this.sendNeutralFrame();

      if (this.bridgeState.clientConnected && this.bridgeState.notifyReady) {
        await this.requestStopState({
          softStop: false,
          estop: false
        });
        await this.requestRemoteMode('MONITOR');
      }

      stopBleSession();
      uni.reLaunch({
        url: '/pages/index/index'
      });
    },
    formatChassisType(value) {
      return String(value || '').toLowerCase() === 'diff' ? '线性差速' : '线性阿克曼';
    },
    formatDriveAxle(value) {
      return String(value || '').toLowerCase() === 'fwd' ? '前驱' : '后驱';
    },
    formatPedalConfig(value) {
      return String(value || '').toLowerCase() === 'estop_throttle'
        ? '左急停 / 右单踏板'
        : '左刹车 / 右油门';
    }
  }
};
</script>

<style lang="scss" scoped>
.page-shell {
  min-height: 100vh;
  background: #f2f6fc;
}

.safe-top {
  width: 100%;
  background: #f2f6fc;
}

.dashboard-shell {
  min-height: calc(100vh - 80px);
  padding-bottom: calc(96px + env(safe-area-inset-bottom));
  box-sizing: border-box;
}

.page-narrow {
  width: 100%;
  max-width: 420px;
  margin: 0 auto;
  padding: 0 16px;
  box-sizing: border-box;
}

.dashboard-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 0 14px;
}

.head-copy {
  min-width: 0;
  flex: 1;
}

.head-title {
  display: block;
  font-size: 26px;
  font-weight: 700;
  color: #303133;
}

.head-subtitle {
  display: block;
  margin-top: 6px;
  font-size: 13px;
  color: #909399;
  word-break: break-all;
}

.head-action {
  width: 40px;
  height: 40px;
  border-radius: 14px;
  background: #ffffff;
  border: 1px solid #fde2e2;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 8px 18px rgba(16, 24, 40, 0.04);
}

.dashboard-scroll {
  max-height: calc(100vh - 170px);
}

.tab-stack {
  padding-bottom: 12px;
}

.panel-card {
  margin-bottom: 16px;
  overflow: hidden;
  border: 1px solid #ebeef5;
  border-radius: 16px;
  background: #ffffff;
  box-shadow: 0 8px 24px rgba(16, 24, 40, 0.04);
}

.card-head {
  min-height: 48px;
  padding: 0 16px;
  border-bottom: 1px solid #ebeef5;
  background: #f8fafc;
  display: flex;
  align-items: center;
}

.card-head-title {
  font-size: 14px;
  font-weight: 600;
  color: #606266;
}

.panel-body {
  padding: 18px 16px;
}

.profile-hero {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding-bottom: 18px;
}

.profile-hero-icon {
  width: 58px;
  height: 58px;
  border-radius: 18px;
  background: #ecf5ff;
  display: flex;
  align-items: center;
  justify-content: center;
}

.profile-name {
  margin-top: 12px;
  font-size: 18px;
  font-weight: 700;
  color: #303133;
}

.profile-code {
  margin-top: 6px;
  font-size: 12px;
  color: #909399;
}

.info-list {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.info-list-tight {
  margin-top: 16px;
}

.info-row {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
}

.info-label {
  font-size: 13px;
  color: #909399;
}

.info-value {
  flex: 1;
  text-align: right;
  font-size: 13px;
  line-height: 18px;
  color: #303133;
  word-break: break-all;
}

.metric-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}

.metric-card {
  padding: 14px 12px;
  border-radius: 14px;
  background: #f8fafc;
}

.metric-label {
  display: block;
  font-size: 12px;
  color: #909399;
}

.metric-value {
  display: block;
  margin-top: 8px;
  font-size: 22px;
  line-height: 24px;
  font-weight: 700;
  color: #303133;
}

.metric-value-small {
  font-size: 15px;
  line-height: 20px;
}

.metric-unit {
  display: block;
  margin-top: 6px;
  font-size: 11px;
  color: #c0c4cc;
}

.status-banner {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
}

.status-badge {
  padding: 4px 10px;
  border-radius: 999px;
}

.badge-online {
  background: #f0f9eb;
}

.badge-offline {
  background: #fef0f0;
}

.status-badge-text {
  font-size: 11px;
  color: #67c23a;
}

.badge-offline .status-badge-text {
  color: #f56c6c;
}

.status-summary {
  margin-top: 10px;
  font-size: 13px;
  line-height: 19px;
  color: #606266;
}

.status-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
  margin-top: 16px;
}

.status-chip {
  padding: 14px 12px;
  border-radius: 14px;
  border: 1px solid #ebeef5;
}

.chip-on {
  background: #f8fff8;
  border-color: #d6f5dc;
}

.chip-off {
  background: #fafafa;
}

.status-chip-title {
  display: block;
  font-size: 12px;
  color: #909399;
}

.status-chip-value {
  display: block;
  margin-top: 8px;
  font-size: 15px;
  font-weight: 600;
  color: #303133;
}

.authority-row {
  display: flex;
  gap: 10px;
  flex-wrap: wrap;
}

.authority-pill {
  padding: 8px 12px;
  border-radius: 999px;
}

.pill-blue {
  background: #ecf5ff;
}

.pill-green {
  background: #f0f9eb;
}

.pill-gray {
  background: #f5f7fa;
}

.authority-pill-text {
  font-size: 12px;
  color: #303133;
}

.gear-panel {
  display: flex;
  flex-direction: column;
}

.gear-copy-title {
  display: block;
  font-size: 14px;
  font-weight: 600;
  color: #303133;
}

.gear-copy-desc {
  display: block;
  margin-top: 6px;
  font-size: 12px;
  line-height: 18px;
  color: #909399;
}

.gear-track {
  position: relative;
  width: 204px;
  height: 46px;
  margin: 16px auto 0;
  padding: 4px;
  border-radius: 999px;
  background: #eef3f8;
  display: flex;
  align-items: center;
  justify-content: space-between;
  box-sizing: border-box;
}

.gear-track-bg {
  position: absolute;
  inset: 4px;
  border-radius: 999px;
  background: linear-gradient(90deg, rgba(103, 194, 58, 0.08), rgba(64, 158, 255, 0.08), rgba(230, 162, 60, 0.08), rgba(245, 108, 108, 0.08));
}

.gear-knob {
  position: absolute;
  top: 4px;
  width: 62px;
  height: 38px;
  border-radius: 999px;
  background: #ffffff;
  box-shadow: 0 8px 18px rgba(16, 24, 40, 0.08);
  transition: left 0.18s ease;
}

.gear-slot {
  position: relative;
  z-index: 2;
  width: 62px;
  text-align: center;
}

.gear-slot-text {
  font-size: 15px;
  font-weight: 700;
  color: #909399;
}

.gear-slot.active .gear-slot-text {
  color: #303133;
}

.control-metrics {
  margin-top: 18px;
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 10px;
}

.control-metric {
  padding: 12px 10px;
  border-radius: 14px;
  background: #f8fafc;
  text-align: center;
}

.control-metric-label {
  display: block;
  font-size: 11px;
  color: #909399;
}

.control-metric-value {
  display: block;
  margin-top: 8px;
  font-size: 22px;
  font-weight: 700;
  color: #409eff;
}

.control-pad-card {
  padding: 18px 16px;
}

.control-pad-wrap {
  position: relative;
  min-height: 280px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.joystick-base {
  position: relative;
  width: 220px;
  height: 220px;
  border-radius: 50%;
  border: 1px solid #dcdfe6;
  background: #f5f7fa;
  box-shadow: inset 0 2px 8px rgba(16, 24, 40, 0.03);
}

.joystick-disabled {
  opacity: 0.52;
}

.joystick-cross {
  position: absolute;
  background: #e4e7ed;
}

.joystick-cross-x {
  left: 0;
  right: 0;
  top: 50%;
  height: 1px;
  transform: translateY(-0.5px);
}

.joystick-cross-y {
  top: 0;
  bottom: 0;
  left: 50%;
  width: 1px;
  transform: translateX(-0.5px);
}

.joystick-knob {
  position: absolute;
  left: 80px;
  top: 80px;
  width: 60px;
  height: 60px;
  border-radius: 50%;
  background: #409eff;
  box-shadow: 0 10px 20px rgba(64, 158, 255, 0.32);
  display: flex;
  align-items: center;
  justify-content: center;
}

.joystick-knob-neutral {
  background: #909399;
  box-shadow: none;
}

.joystick-knob-core {
  width: 20px;
  height: 20px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.32);
}

.estop-mask {
  position: absolute;
  inset: 0;
  border-radius: 16px;
  background: rgba(255, 255, 255, 0.8);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}

.estop-mask-text {
  margin-top: 10px;
  font-size: 13px;
  color: #f56c6c;
}

.stop-row {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
  margin-top: 8px;
}

.stop-button {
  height: 48px;
  border-radius: 14px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.stop-button-orange {
  background: #e6a23c;
  box-shadow: 0 12px 24px rgba(230, 162, 60, 0.18);
}

.stop-button-red {
  background: #f56c6c;
  box-shadow: 0 12px 24px rgba(245, 108, 108, 0.18);
}

.stop-button-active {
  opacity: 0.74;
}

.stop-button-text {
  font-size: 15px;
  font-weight: 600;
  color: #ffffff;
}

.button-row {
  margin-top: 8px;
}

.button-row-stack {
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.primary-button,
.ghost-button {
  height: 48px;
  border-radius: 14px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.wide-button {
  width: 100%;
}

.primary-button {
  background: #409eff;
  box-shadow: 0 12px 24px rgba(64, 158, 255, 0.18);
}

.primary-button-text {
  font-size: 15px;
  font-weight: 600;
  color: #ffffff;
}

.ghost-button {
  border: 1px solid #c6e2ff;
  background: #ecf5ff;
}

.ghost-button-text {
  font-size: 14px;
  color: #606266;
}

.ghost-button-text-blue {
  color: #409eff;
}

.form-stack {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.form-group {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.form-label {
  font-size: 13px;
  color: #606266;
}

.choice-row {
  display: flex;
  gap: 10px;
}

.choice-chip {
  flex: 1;
  min-height: 42px;
  border-radius: 12px;
  border: 1px solid #dcdfe6;
  background: #ffffff;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0 10px;
  box-sizing: border-box;
}

.choice-chip.active {
  border-color: #409eff;
  background: #ecf5ff;
}

.choice-chip-text {
  font-size: 13px;
  color: #303133;
}

.number-input {
  height: 44px;
  border-radius: 12px;
  border: 1px solid #dcdfe6;
  background: #ffffff;
  padding: 0 14px;
  font-size: 14px;
  color: #303133;
  box-sizing: border-box;
}

.switch-row {
  padding: 6px 0 4px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.switch-copy {
  flex: 1;
}

.switch-title {
  display: block;
  font-size: 14px;
  font-weight: 600;
  color: #303133;
}

.switch-desc {
  display: block;
  margin-top: 6px;
  font-size: 12px;
  line-height: 18px;
  color: #909399;
}

.switch-shell {
  width: 52px;
  height: 30px;
  border-radius: 999px;
  background: #dcdfe6;
  padding: 3px;
  box-sizing: border-box;
}

.switch-shell.active {
  background: #409eff;
}

.switch-shell.disabled {
  opacity: 0.5;
}

.switch-knob {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  background: #ffffff;
  transition: transform 0.18s ease;
}

.switch-shell.active .switch-knob {
  transform: translateX(22px);
}

.status-inline {
  padding: 12px 14px;
  border-radius: 12px;
}

.status-inline-normal {
  background: #f0f9eb;
}

.status-inline-warn {
  background: #fdf6ec;
}

.status-inline-text {
  font-size: 12px;
  line-height: 18px;
  color: #606266;
}

.bottom-nav {
  position: fixed;
  left: 50%;
  bottom: 0;
  width: 100%;
  max-width: 420px;
  transform: translateX(-50%);
  padding: 8px 10px calc(10px + env(safe-area-inset-bottom));
  border-top: 1px solid #ebeef5;
  background: rgba(255, 255, 255, 0.96);
  display: flex;
  align-items: center;
  justify-content: space-around;
  box-sizing: border-box;
}

.nav-item {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
}

.nav-item-text {
  margin-top: 4px;
  font-size: 11px;
  color: #909399;
}

.nav-item.active .nav-item-text {
  color: #409eff;
}
</style>
