<template>
  <view class="page-shell">
    <view class="safe-top" :style="{ height: `${statusBarHeight}px` }"></view>

    <view class="page-narrow calibration-shell">
      <view class="calibration-head">
        <view class="back-button" @tap="goBack">
          <view class="back-icon-flip">
            <AppSvgIcon name="chevron" :size="18" color="#303133" />
          </view>
        </view>

        <view class="head-copy">
          <text class="head-title">线性矫正</text>
          <text class="head-subtitle">{{ connectedProfile ? connectedProfile.name : '未连接底盘' }}</text>
        </view>
      </view>

      <scroll-view scroll-y class="calibration-scroll">
        <view class="status-inline" :class="vehicleMoving ? 'status-inline-warn' : 'status-inline-normal'">
          <text class="status-inline-text">
            {{ vehicleMoving ? '车辆正在运动，矫正采样、转向点动、记录矫正点与保存已锁定。' : '车辆静止，可执行踏板采样、转向矫正与保存下发。' }}
          </text>
        </view>

        <view class="panel-card">
          <view class="card-head">
            <text class="card-head-title">油门刹车矫正</text>
          </view>

          <view class="panel-body">
            <view class="form-group">
              <text class="form-label">踏板映射方式</text>
              <view class="choice-row">
                <view
                  class="choice-chip"
                  :class="{ active: pedalConfig === 'brake_throttle' }"
                  @tap="changePedalConfig('brake_throttle')"
                >
                  <text class="choice-chip-text">左刹车 / 右油门</text>
                </view>
                <view
                  class="choice-chip"
                  :class="{ active: pedalConfig === 'estop_throttle' }"
                  @tap="changePedalConfig('estop_throttle')"
                >
                  <text class="choice-chip-text">左急停 / 右单踏板</text>
                </view>
              </view>
            </view>

            <view class="pedal-grid">
              <view class="pedal-card">
                <text class="pedal-title">左踏板</text>
                <text class="pedal-desc">{{ pedalConfig === 'estop_throttle' ? '左急停踏板' : '左刹车踏板' }}</text>
                <view class="pedal-values">
                  <text class="pedal-value-label">松开均值 {{ calibration.brakeRawMin }}</text>
                  <text class="pedal-value-label">踩下均值 {{ calibration.brakeRawMax }}</text>
                </view>
                <view class="action-button ghost-button" @tap="openPedalWizard('left')">
                  <text class="ghost-button-text">矫正左踏板</text>
                </view>
              </view>

              <view class="pedal-card">
                <text class="pedal-title">右踏板</text>
                <text class="pedal-desc">右油门踏板</text>
                <view class="pedal-values">
                  <text class="pedal-value-label">松开均值 {{ calibration.throttleRawMin }}</text>
                  <text class="pedal-value-label">踩下均值 {{ calibration.throttleRawMax }}</text>
                </view>
                <view class="action-button ghost-button" @tap="openPedalWizard('right')">
                  <text class="ghost-button-text">矫正右踏板</text>
                </view>
              </view>
            </view>
          </view>
        </view>

        <view class="panel-card">
          <view class="card-head">
            <text class="card-head-title">底盘机械转向</text>
          </view>

          <view class="panel-body">
            <view class="estimate-bar">
              <view class="estimate-chip">
                <text class="estimate-label">实时位置</text>
                <text class="estimate-value">{{ steeringEstimate }}</text>
              </view>
              <view class="estimate-chip">
                <text class="estimate-label">CAN 状态</text>
                <text class="estimate-value">{{ latestStatus && latestStatus.steerCanReady ? '已就绪' : '未就绪' }}</text>
              </view>
            </view>

            <view class="jog-row">
              <view
                class="hold-button"
                @touchstart.stop.prevent="startJog('STEERING', -60)"
                @touchend.stop.prevent="stopJog('STEERING')"
                @touchcancel.stop.prevent="stopJog('STEERING')"
                @mousedown.stop.prevent="startJog('STEERING', -60)"
                @mouseup.stop.prevent="stopJog('STEERING')"
                @mouseleave.stop.prevent="stopJog('STEERING')"
              >
                <text class="hold-button-text">按住左转</text>
              </view>
              <view
                class="hold-button"
                @touchstart.stop.prevent="startJog('STEERING', 60)"
                @touchend.stop.prevent="stopJog('STEERING')"
                @touchcancel.stop.prevent="stopJog('STEERING')"
                @mousedown.stop.prevent="startJog('STEERING', 60)"
                @mouseup.stop.prevent="stopJog('STEERING')"
                @mouseleave.stop.prevent="stopJog('STEERING')"
              >
                <text class="hold-button-text">按住右转</text>
              </view>
            </view>

            <view class="point-grid">
              <view class="point-button point-danger" @tap="capturePoint('STEERING', 'LEFT_LIMIT')">
                <text class="point-button-text">左最大限位角</text>
              </view>
              <view class="point-button point-success" @tap="capturePoint('STEERING', 'CENTER')">
                <text class="point-button-text">中心位置</text>
              </view>
              <view class="point-button point-danger" @tap="capturePoint('STEERING', 'RIGHT_LIMIT')">
                <text class="point-button-text">右最大限位角</text>
              </view>
              <view class="point-button point-primary" @tap="capturePoint('STEERING', 'LEFT10')">
                <text class="point-button-text">左 10°</text>
              </view>
              <view class="point-button point-primary" @tap="capturePoint('STEERING', 'RIGHT10')">
                <text class="point-button-text">右 10°</text>
              </view>
            </view>

            <view class="value-grid">
              <view class="value-chip">
                <text class="value-chip-label">左限位</text>
                <text class="value-chip-value">{{ calibration.steeringLeftLimit }}</text>
              </view>
              <view class="value-chip">
                <text class="value-chip-label">中心</text>
                <text class="value-chip-value">{{ calibration.steeringCenter }}</text>
              </view>
              <view class="value-chip">
                <text class="value-chip-label">右限位</text>
                <text class="value-chip-value">{{ calibration.steeringRightLimit }}</text>
              </view>
              <view class="value-chip">
                <text class="value-chip-label">左 10°</text>
                <text class="value-chip-value">{{ calibration.steeringLeft10 }}</text>
              </view>
              <view class="value-chip">
                <text class="value-chip-label">右 10°</text>
                <text class="value-chip-value">{{ calibration.steeringRight10 }}</text>
              </view>
            </view>
          </view>
        </view>

        <view class="panel-card" :class="{ 'disabled-panel': !appConfig.hasLinearSteering || !linearSteeringSupported }">
          <view class="card-head card-head-between">
            <text class="card-head-title">线性方向盘</text>
            <view class="state-pill" :class="appConfig.hasLinearSteering && linearSteeringSupported ? 'state-pill-on' : 'state-pill-off'">
              <text class="state-pill-text">{{ appConfig.hasLinearSteering && linearSteeringSupported ? '已启用' : '未启用' }}</text>
            </view>
          </view>

          <view class="panel-body">
            <view class="estimate-bar">
              <view class="estimate-chip">
                <text class="estimate-label">实时位置</text>
                <text class="estimate-value">{{ linearSteeringSupported ? handwheelEstimate : '--' }}</text>
              </view>
              <view class="estimate-chip">
                <text class="estimate-label">检测状态</text>
                <text class="estimate-value">
                  {{ linearSteeringSupported ? (linearSteeringState.detected ? '已检测' : '未检测') : '未接协议' }}
                </text>
              </view>
            </view>

            <view v-if="linearSteeringSupported" class="jog-row">
              <view
                class="hold-button hold-button-warn"
                @touchstart.stop.prevent="startJog('HANDWHEEL', -60)"
                @touchend.stop.prevent="stopJog('HANDWHEEL')"
                @touchcancel.stop.prevent="stopJog('HANDWHEEL')"
                @mousedown.stop.prevent="startJog('HANDWHEEL', -60)"
                @mouseup.stop.prevent="stopJog('HANDWHEEL')"
                @mouseleave.stop.prevent="stopJog('HANDWHEEL')"
              >
                <text class="hold-button-text">方向盘左打</text>
              </view>
              <view
                class="hold-button hold-button-warn"
                @touchstart.stop.prevent="startJog('HANDWHEEL', 60)"
                @touchend.stop.prevent="stopJog('HANDWHEEL')"
                @touchcancel.stop.prevent="stopJog('HANDWHEEL')"
                @mousedown.stop.prevent="startJog('HANDWHEEL', 60)"
                @mouseup.stop.prevent="stopJog('HANDWHEEL')"
                @mouseleave.stop.prevent="stopJog('HANDWHEEL')"
              >
                <text class="hold-button-text">方向盘右打</text>
              </view>
            </view>

            <view v-if="linearSteeringSupported" class="point-grid">
              <view class="point-button point-danger" @tap="capturePoint('HANDWHEEL', 'LEFT_LIMIT')">
                <text class="point-button-text">左最大限位角</text>
              </view>
              <view class="point-button point-success" @tap="capturePoint('HANDWHEEL', 'CENTER')">
                <text class="point-button-text">方向盘居中</text>
              </view>
              <view class="point-button point-danger" @tap="capturePoint('HANDWHEEL', 'RIGHT_LIMIT')">
                <text class="point-button-text">右最大限位角</text>
              </view>
              <view class="point-button point-primary" @tap="capturePoint('HANDWHEEL', 'LEFT10')">
                <text class="point-button-text">左 10°</text>
              </view>
              <view class="point-button point-primary" @tap="capturePoint('HANDWHEEL', 'RIGHT10')">
                <text class="point-button-text">右 10°</text>
              </view>
            </view>

            <view v-if="!linearSteeringSupported" class="disabled-note">
              <text class="disabled-note-text">当前固件未接入线性方向盘协议能力，这一栏仅保留结构位置。</text>
            </view>

            <view class="value-grid">
              <view class="value-chip">
                <text class="value-chip-label">左限位</text>
                <text class="value-chip-value">{{ calibration.handwheelLeftLimit }}</text>
              </view>
              <view class="value-chip">
                <text class="value-chip-label">中心</text>
                <text class="value-chip-value">{{ calibration.handwheelCenter }}</text>
              </view>
              <view class="value-chip">
                <text class="value-chip-label">右限位</text>
                <text class="value-chip-value">{{ calibration.handwheelRightLimit }}</text>
              </view>
              <view class="value-chip">
                <text class="value-chip-label">左 10°</text>
                <text class="value-chip-value">{{ calibration.handwheelLeft10 }}</text>
              </view>
              <view class="value-chip">
                <text class="value-chip-label">右 10°</text>
                <text class="value-chip-value">{{ calibration.handwheelRight10 }}</text>
              </view>
            </view>
          </view>
        </view>
      </scroll-view>
    </view>

    <view class="bottom-save-bar">
      <view class="primary-button wide-button" @tap="saveCalibration">
        <text class="primary-button-text">{{ savingCalibration ? '保存中...' : '独立保存并下发矫正表' }}</text>
      </view>
    </view>

    <view v-if="pedalWizard.visible" class="modal-mask">
      <view class="modal-panel">
        <view class="modal-head">
          <text class="modal-title">{{ pedalWizard.pedal === 'left' ? '左踏板' : '右踏板' }}采样向导</text>
          <text class="modal-step">{{ pedalWizardFinished ? '完成' : `${pedalWizardCycle} / 5` }}</text>
        </view>

        <view class="modal-body">
          <view v-if="!pedalWizardFinished" class="wizard-state">
            <text class="wizard-title">{{ pedalWizardIsPressed ? '请踩到底' : '请完全松开' }}</text>
            <text class="wizard-desc">到位后点击下方按钮采样，本次会执行 5 轮踩下 / 松开平均。</text>
          </view>

          <view v-else class="wizard-state">
            <text class="wizard-title">采样完成</text>
            <text class="wizard-desc">最新平均值 {{ pedalWizard.lastAverage }}，现在可以保存整张矫正表。</text>
          </view>

          <view class="wizard-data">
            <text class="wizard-data-line">最新原始值 {{ pedalWizard.lastRaw }}</text>
            <text class="wizard-data-line">当前计数 {{ pedalWizard.lastCount }}</text>
            <text class="wizard-data-line">当前平均 {{ pedalWizard.lastAverage }}</text>
          </view>
        </view>

        <view class="modal-actions">
          <view class="ghost-button modal-button" @tap="closePedalWizard">
            <text class="ghost-button-text">{{ pedalWizardFinished ? '完成' : '中止' }}</text>
          </view>
          <view v-if="!pedalWizardFinished" class="primary-button modal-button" @tap="capturePedalWizardStep">
            <text class="primary-button-text">{{ pedalWizard.busy ? '采样中...' : '已到位，采样' }}</text>
          </view>
        </view>
      </view>
    </view>
  </view>
</template>

<script>
import AppSvgIcon from '@/components/AppSvgIcon.vue';
import {
  createGetCalibrationCommand,
  createGetCapabilitiesCommand,
  createGetConfigCommand,
  createGetStatusCommand,
  createPedalSampleCommand,
  createQueryLinearSteeringCommand,
  createSaveCalibrationCommand,
  createSetConfigCommand,
  createCalibrationPointCommand,
  createSteeringJogCommand
} from '@/utils/chassis-protocol';
import { syncSessionFromBridgeState } from '@/utils/bridge-session-sync';
import {
  getAppConfig,
  getCapabilities,
  getCalibration,
  getConnectedProfile,
  getSessionState,
  persistConnectedProfile,
  updateAppConfig
} from '@/utils/session-state';
import { getBridgeState, sendJsonCommand } from '@/utils/ewm22-ble-bridge';
import { getLatestStatusSnapshot } from '@/utils/bridge-status';

export default {
  components: {
    AppSvgIcon
  },
  data() {
    return {
      statusBarHeight: 0,
      bridgeState: getBridgeState(),
      sessionState: getSessionState(),
      connectedProfile: getConnectedProfile(),
      syncTicker: null,
      lastStatusQueryAt: 0,
      lastCalibrationQueryAt: 0,
      lastMetaQueryAt: 0,
      savingCalibration: false,
      pedalConfig: getAppConfig().pedalConfig || 'brake_throttle',
      steeringEstimate: getCalibration().steeringEstimate || 0,
      handwheelEstimate: getCalibration().handwheelEstimate || 0,
      pedalWizard: {
        visible: false,
        pedal: 'left',
        stepIndex: 0,
        busy: false,
        lastRaw: 0,
        lastCount: 0,
        lastAverage: 0
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
    calibration() {
      return this.sessionState.calibration || getCalibration();
    },
    linearSteeringState() {
      return this.sessionState.linearSteeringState || {};
    },
    latestStatus() {
      return getLatestStatusSnapshot(this.bridgeState);
    },
    vehicleMoving() {
      return !!(this.latestStatus && this.latestStatus.vehicleMoving);
    },
    pedalWizardFinished() {
      return this.pedalWizard.stepIndex >= 10;
    },
    pedalWizardCycle() {
      return Math.floor(this.pedalWizard.stepIndex / 2) + 1;
    },
    pedalWizardIsPressed() {
      return (this.pedalWizard.stepIndex % 2) === 0;
    }
  },
  onLoad() {
    try {
      const systemInfo = uni.getSystemInfoSync();
      this.statusBarHeight = Number(systemInfo.statusBarHeight || 0);
    } catch (error) {
      this.statusBarHeight = 0;
    }

    if (!this.connectedProfile) {
      uni.reLaunch({
        url: '/pages/index/index'
      });
      return;
    }

    this.startSyncTicker();
    this.requestSnapshot();
  },
  onShow() {
    this.bridgeState = getBridgeState();
    this.connectedProfile = getConnectedProfile();
    this.sessionState = syncSessionFromBridgeState(this.bridgeState);
    this.pedalConfig = this.appConfig.pedalConfig || 'brake_throttle';
    this.requestSnapshot();
  },
  onUnload() {
    clearInterval(this.syncTicker);
    this.syncTicker = null;
    this.stopAllJog();
  },
  methods: {
    startSyncTicker() {
      clearInterval(this.syncTicker);
      this.syncTicker = setInterval(() => {
        this.bridgeState = getBridgeState();
        this.connectedProfile = getConnectedProfile();
        this.sessionState = syncSessionFromBridgeState(this.bridgeState);

        if (this.latestStatus && this.latestStatus.steeringFeedbackValid) {
          this.steeringEstimate = this.latestStatus.steeringFeedback;
        } else {
          this.steeringEstimate = this.calibration.steeringEstimate;
        }

        this.handwheelEstimate = this.calibration.handwheelEstimate;

        if (this.pedalWizard.visible && this.bridgeState.pedalSample) {
          this.pedalWizard.lastRaw = Number(this.bridgeState.pedalSample.raw || 0);
          this.pedalWizard.lastCount = Number(this.bridgeState.pedalSample.count || 0);
          this.pedalWizard.lastAverage = Number(this.bridgeState.pedalSample.average || 0);
        }

        const now = Date.now();
        if ((now - this.lastStatusQueryAt) > 500) {
          this.lastStatusQueryAt = now;
          sendJsonCommand(createGetStatusCommand());
        }

        if ((now - this.lastCalibrationQueryAt) > 1500) {
          this.lastCalibrationQueryAt = now;
          sendJsonCommand(createGetCalibrationCommand());
        }

        if ((now - this.lastMetaQueryAt) > 2500) {
          this.lastMetaQueryAt = now;
          sendJsonCommand(createGetCapabilitiesCommand());
          sendJsonCommand(createGetConfigCommand());
          sendJsonCommand(createQueryLinearSteeringCommand());
        }
      }, 180);
    },
    requestSnapshot() {
      sendJsonCommand(createGetCapabilitiesCommand());
      sendJsonCommand(createGetConfigCommand());
      sendJsonCommand(createGetStatusCommand());
      sendJsonCommand(createGetCalibrationCommand());
      sendJsonCommand(createQueryLinearSteeringCommand());
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
    buildRuntimeConfig() {
      return {
        ...this.appConfig,
        hasLinearSteering: !!this.appConfig.hasLinearSteering && this.linearSteeringSupported,
        pedalConfig: this.pedalConfig
      };
    },
    changePedalConfig(mode) {
      this.pedalConfig = mode;
    },
    openPedalWizard(pedal) {
      if (!this.ensureVehicleStopped()) {
        return;
      }

      this.pedalWizard = {
        visible: true,
        pedal,
        stepIndex: 0,
        busy: false,
        lastRaw: 0,
        lastCount: 0,
        lastAverage: 0
      };
    },
    closePedalWizard() {
      this.pedalWizard.visible = false;
      this.pedalWizard.busy = false;
    },
    async capturePedalWizardStep() {
      if (this.pedalWizard.busy || this.pedalWizardFinished) {
        return;
      }

      if (!this.ensureVehicleStopped()) {
        return;
      }

      const pedal = this.pedalWizard.pedal;
      const phase = this.pedalWizardIsPressed ? 'PRESSED' : 'RELEASED';
      const requestAt = Date.now();

      this.pedalWizard.busy = true;
      sendJsonCommand(createPedalSampleCommand(pedal, phase, {
        reset: this.pedalWizard.stepIndex === 0
      }), {
        replacePendingQueue: true
      });

      const ok = await this.waitForCondition((bridgeState) => {
        if (!bridgeState.pedalSampleAt || bridgeState.pedalSampleAt < requestAt || !bridgeState.pedalSample) {
          return false;
        }

        return String(bridgeState.pedalSample.pedal || '').toUpperCase() === pedal.toUpperCase() &&
          String(bridgeState.pedalSample.phase || '').toUpperCase() === phase;
      }, 1800);

      this.pedalWizard.busy = false;

      if (!ok) {
        if (this.bridgeState.pedalSample && this.bridgeState.pedalSample.error === 'vehicle_moving') {
          this.showVehicleMovingModal();
          return;
        }

        uni.showToast({
          title: '踏板采样超时',
          icon: 'none'
        });
        return;
      }

      this.pedalWizard.stepIndex += 1;
      sendJsonCommand(createGetCalibrationCommand());

      if (this.pedalWizardFinished) {
        uni.showToast({
          title: '踏板采样完成',
          icon: 'success'
        });
      }
    },
    async startJog(axis, rpm) {
      if (!this.ensureVehicleStopped()) {
        return;
      }

      if (axis === 'HANDWHEEL' && (!this.appConfig.hasLinearSteering || !this.linearSteeringSupported)) {
        return;
      }

      sendJsonCommand(createSteeringJogCommand(axis, rpm), {
        replacePendingQueue: true
      });
    },
    stopJog(axis) {
      if (axis === 'HANDWHEEL' && (!this.appConfig.hasLinearSteering || !this.linearSteeringSupported)) {
        return;
      }

      sendJsonCommand(createSteeringJogCommand(axis, 0), {
        replacePendingQueue: true
      });
    },
    stopAllJog() {
      sendJsonCommand(createSteeringJogCommand('STEERING', 0));
      if (this.linearSteeringSupported && this.appConfig.hasLinearSteering) {
        sendJsonCommand(createSteeringJogCommand('HANDWHEEL', 0));
      }
    },
    async capturePoint(axis, point) {
      if (!this.ensureVehicleStopped()) {
        return;
      }

      if (axis === 'HANDWHEEL' && (!this.appConfig.hasLinearSteering || !this.linearSteeringSupported)) {
        return;
      }

      const requestAt = Date.now();
      sendJsonCommand(createCalibrationPointCommand(axis, point), {
        replacePendingQueue: true
      });

      const ok = await this.waitForCondition((bridgeState) => {
        return !!bridgeState.lastJsonAt &&
          bridgeState.lastJsonAt >= requestAt &&
          bridgeState.lastJson &&
          bridgeState.lastJson.cmd === 'set_calibration_point' &&
          bridgeState.lastJson.ok;
      }, 1500);

      if (!ok) {
        if (this.bridgeState.lastJson && this.bridgeState.lastJson.error === 'vehicle_moving') {
          this.showVehicleMovingModal();
          return;
        }

        uni.showToast({
          title: '矫正点下发失败',
          icon: 'none'
        });
        return;
      }

      sendJsonCommand(createGetCalibrationCommand());
      uni.showToast({
        title: '已记录矫正点',
        icon: 'success'
      });
    },
    async saveCalibration() {
      if (this.savingCalibration) {
        return;
      }

      if (!this.ensureVehicleStopped()) {
        return;
      }

      this.savingCalibration = true;

      sendJsonCommand(createSetConfigCommand(this.buildRuntimeConfig()), {
        replacePendingQueue: true
      });

      const requestAt = Date.now();
      sendJsonCommand(createSaveCalibrationCommand());

      const ok = await this.waitForCondition((bridgeState) => {
        return !!bridgeState.saveResultAt &&
          bridgeState.saveResultAt >= requestAt &&
          bridgeState.saveResult &&
          bridgeState.saveResult.domain === 'calibration';
      }, 2200);

      this.savingCalibration = false;

      if (!ok) {
        uni.showToast({
          title: '矫正保存超时',
          icon: 'none'
        });
        return;
      }

      if (this.bridgeState.saveResult && this.bridgeState.saveResult.ok) {
        updateAppConfig({
          pedalConfig: this.pedalConfig
        });
        persistConnectedProfile(this.bridgeState);
        this.requestSnapshot();
        uni.showToast({
          title: '矫正表已保存',
          icon: 'success'
        });
        return;
      }

      if (this.bridgeState.saveResult && this.bridgeState.saveResult.error === 'vehicle_moving') {
        this.showVehicleMovingModal();
        return;
      }

      uni.showToast({
        title: '矫正保存失败',
        icon: 'none'
      });
    },
    goBack() {
      this.stopAllJog();
      uni.navigateBack({
        delta: 1
      });
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

.page-narrow {
  width: 100%;
  max-width: 420px;
  margin: 0 auto;
  padding: 0 16px;
  box-sizing: border-box;
}

.calibration-shell {
  padding-bottom: calc(100px + env(safe-area-inset-bottom));
}

.calibration-head {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 0 14px;
}

.back-button {
  width: 40px;
  height: 40px;
  border-radius: 14px;
  background: #ffffff;
  border: 1px solid #ebeef5;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 8px 18px rgba(16, 24, 40, 0.04);
}

.back-icon-flip {
  transform: rotate(180deg);
  display: flex;
  align-items: center;
  justify-content: center;
}

.head-copy {
  flex: 1;
  min-width: 0;
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

.calibration-scroll {
  max-height: calc(100vh - 166px);
}

.status-inline {
  margin-bottom: 16px;
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

.panel-card {
  margin-bottom: 16px;
  overflow: hidden;
  border: 1px solid #ebeef5;
  border-radius: 16px;
  background: #ffffff;
  box-shadow: 0 8px 24px rgba(16, 24, 40, 0.04);
}

.disabled-panel {
  opacity: 0.46;
}

.disabled-note {
  margin-top: 16px;
  padding: 14px 12px;
  border-radius: 12px;
  background: #f5f7fa;
}

.disabled-note-text {
  font-size: 12px;
  line-height: 18px;
  color: #909399;
}

.card-head {
  min-height: 48px;
  padding: 0 16px;
  border-bottom: 1px solid #ebeef5;
  background: #f8fafc;
  display: flex;
  align-items: center;
}

.card-head-between {
  justify-content: space-between;
}

.card-head-title {
  font-size: 14px;
  font-weight: 600;
  color: #606266;
}

.state-pill {
  padding: 4px 10px;
  border-radius: 999px;
}

.state-pill-on {
  background: #f0f9eb;
}

.state-pill-off {
  background: #f5f7fa;
}

.state-pill-text {
  font-size: 11px;
  color: #67c23a;
}

.state-pill-off .state-pill-text {
  color: #909399;
}

.panel-body {
  padding: 18px 16px;
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
  min-height: 44px;
  padding: 8px 10px;
  border-radius: 12px;
  border: 1px solid #dcdfe6;
  background: #ffffff;
  display: flex;
  align-items: center;
  justify-content: center;
  box-sizing: border-box;
}

.choice-chip.active {
  border-color: #409eff;
  background: #ecf5ff;
}

.choice-chip-text {
  font-size: 13px;
  line-height: 18px;
  text-align: center;
  color: #303133;
}

.pedal-grid {
  margin-top: 18px;
  display: grid;
  grid-template-columns: 1fr;
  gap: 12px;
}

.pedal-card {
  padding: 16px 14px;
  border-radius: 14px;
  background: #f8fafc;
}

.pedal-title {
  display: block;
  font-size: 15px;
  font-weight: 700;
  color: #303133;
}

.pedal-desc {
  display: block;
  margin-top: 6px;
  font-size: 12px;
  color: #909399;
}

.pedal-values {
  margin-top: 12px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.pedal-value-label {
  font-size: 12px;
  color: #606266;
}

.action-button {
  margin-top: 14px;
}

.ghost-button,
.primary-button,
.hold-button,
.point-button {
  display: flex;
  align-items: center;
  justify-content: center;
}

.ghost-button {
  height: 44px;
  border-radius: 12px;
  border: 1px solid #c6e2ff;
  background: #ecf5ff;
}

.ghost-button-text {
  font-size: 13px;
  color: #409eff;
}

.estimate-bar {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}

.estimate-chip {
  padding: 14px 12px;
  border-radius: 14px;
  background: #f8fafc;
}

.estimate-label {
  display: block;
  font-size: 12px;
  color: #909399;
}

.estimate-value {
  display: block;
  margin-top: 8px;
  font-size: 18px;
  font-weight: 700;
  color: #303133;
}

.jog-row {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
  margin-top: 16px;
}

.hold-button {
  height: 46px;
  border-radius: 12px;
  background: #409eff;
  box-shadow: 0 10px 18px rgba(64, 158, 255, 0.18);
}

.hold-button-warn {
  background: #e6a23c;
  box-shadow: 0 10px 18px rgba(230, 162, 60, 0.18);
}

.hold-button-text {
  font-size: 14px;
  font-weight: 600;
  color: #ffffff;
}

.point-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
  margin-top: 16px;
}

.point-button {
  min-height: 44px;
  padding: 8px 10px;
  border-radius: 12px;
  border: 1px solid transparent;
  box-sizing: border-box;
}

.point-button-text {
  font-size: 13px;
  line-height: 18px;
  text-align: center;
}

.point-primary {
  background: #ecf5ff;
  border-color: #b3d8ff;
}

.point-primary .point-button-text {
  color: #409eff;
}

.point-success {
  background: #f0f9eb;
  border-color: #d6f5dc;
}

.point-success .point-button-text {
  color: #67c23a;
}

.point-danger {
  background: #fef0f0;
  border-color: #fde2e2;
}

.point-danger .point-button-text {
  color: #f56c6c;
}

.value-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
  margin-top: 16px;
}

.value-chip {
  padding: 12px 10px;
  border-radius: 12px;
  background: #f8fafc;
}

.value-chip-label {
  display: block;
  font-size: 11px;
  color: #909399;
}

.value-chip-value {
  display: block;
  margin-top: 8px;
  font-size: 16px;
  font-weight: 700;
  color: #303133;
}

.bottom-save-bar {
  position: fixed;
  left: 50%;
  bottom: 0;
  width: 100%;
  max-width: 420px;
  transform: translateX(-50%);
  padding: 10px 16px calc(10px + env(safe-area-inset-bottom));
  background: rgba(255, 255, 255, 0.96);
  border-top: 1px solid #ebeef5;
  box-sizing: border-box;
}

.wide-button {
  width: 100%;
}

.primary-button {
  height: 48px;
  border-radius: 14px;
  background: #303133;
  box-shadow: 0 12px 24px rgba(48, 49, 51, 0.16);
}

.primary-button-text {
  font-size: 15px;
  font-weight: 600;
  color: #ffffff;
}

.modal-mask {
  position: fixed;
  inset: 0;
  background: rgba(0, 0, 0, 0.42);
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 16px;
  box-sizing: border-box;
}

.modal-panel {
  width: 100%;
  max-width: 360px;
  border-radius: 16px;
  background: #ffffff;
  overflow: hidden;
}

.modal-head {
  padding: 16px;
  border-bottom: 1px solid #ebeef5;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.modal-title {
  font-size: 15px;
  font-weight: 700;
  color: #303133;
}

.modal-step {
  font-size: 12px;
  color: #909399;
}

.modal-body {
  padding: 20px 16px;
}

.wizard-state {
  text-align: center;
}

.wizard-title {
  display: block;
  font-size: 20px;
  font-weight: 700;
  color: #303133;
}

.wizard-desc {
  display: block;
  margin-top: 10px;
  font-size: 12px;
  line-height: 18px;
  color: #909399;
}

.wizard-data {
  margin-top: 18px;
  padding: 14px 12px;
  border-radius: 12px;
  background: #f8fafc;
}

.wizard-data-line {
  display: block;
  font-size: 12px;
  line-height: 18px;
  color: #606266;
}

.wizard-data-line + .wizard-data-line {
  margin-top: 4px;
}

.modal-actions {
  padding: 0 16px 16px;
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}

.modal-button {
  width: 100%;
}
</style>
