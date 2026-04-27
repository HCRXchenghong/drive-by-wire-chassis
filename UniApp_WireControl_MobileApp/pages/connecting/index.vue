<template>
  <view class="page-shell">
    <view class="safe-top" :style="{ height: `${statusBarHeight}px` }"></view>

    <view class="page-body page-narrow">
      <view class="center-title-block">
        <text class="center-title">建立设备连接</text>
        <text class="center-subtitle">{{ pendingProfile ? pendingProfile.name : '目标底盘' }}</text>
      </view>

      <view class="panel-card connect-card">
        <view class="progress-copy-row">
          <text class="progress-copy-label">{{ progress === 100 ? '连接成功' : '系统连接中...' }}</text>
          <text class="progress-copy-value">{{ progress }}%</text>
        </view>
        <view class="progress-track">
          <view class="progress-bar" :style="{ width: `${progress}%` }"></view>
        </view>

        <view class="steps-list">
          <view v-for="(step, index) in stepsList" :key="step.title" class="step-row">
            <view v-if="index < stepsList.length - 1" class="step-line" :class="{ active: activeStep > index }"></view>

            <view class="step-icon-wrap">
              <view v-if="activeStep > index" class="step-icon step-done">
                <AppSvgIcon name="check" :size="16" color="#ffffff" />
              </view>
              <view v-else-if="activeStep === index" class="step-icon step-current">
                <view class="step-pulse"></view>
              </view>
              <view v-else class="step-icon step-pending"></view>
            </view>

            <view class="step-copy">
              <text class="step-title" :class="{ inactive: activeStep < index }">{{ step.title }}</text>
              <text class="step-desc" :class="{ current: activeStep === index }">{{ step.desc }}</text>
            </view>
          </view>
        </view>

        <view v-if="connectError" class="error-panel">
          <AppSvgIcon name="bluetooth-off" :size="18" color="#F56C6C" />
          <text class="error-text">{{ connectError }}</text>
        </view>

        <view class="footer-row">
          <view class="ghost-button" @tap="cancelConnect">
            <text class="ghost-button-text">取消连接</text>
          </view>
        </view>
      </view>
    </view>
  </view>
</template>

<script>
import AppSvgIcon from '@/components/AppSvgIcon.vue';
import { syncSessionFromBridgeState } from '@/utils/bridge-session-sync';
import { createControlFrame, createGetCalibrationCommand, createGetCapabilitiesCommand, createGetConfigCommand, createGetStatusCommand, createQueryLinearSteeringCommand } from '@/utils/chassis-protocol';
import { getPendingProfile, persistConnectedProfile, setConnectedProfile, updateAppConfig } from '@/utils/session-state';
import { getBridgeState, sendControlFrame, sendJsonCommand, startBleSession, stopBleSession } from '@/utils/ewm22-ble-bridge';

export default {
  components: {
    AppSvgIcon
  },
  data() {
    return {
      statusBarHeight: 0,
      source: 'profile',
      pendingProfile: null,
      bridgeState: getBridgeState(),
      progress: 10,
      activeStep: 0,
      connectError: '',
      connectTicker: null,
      connectStartedAt: 0,
      connectPhase: 0,
      snapshotRequestSent: false,
      frameSeq: 1,
      stepsList: [
        { title: '蓝牙准备', desc: '打开手机蓝牙并开始搜索' },
        { title: '设备发现', desc: '扫描目标 EWM22 BLE 模块' },
        { title: '建立链路', desc: '建立 BLE 透传连接' },
        { title: '协议握手', desc: '请求 STM32 配置与状态快照' }
      ]
    };
  },
  onLoad(options) {
    try {
      const systemInfo = uni.getSystemInfoSync();
      this.statusBarHeight = Number(systemInfo.statusBarHeight || 0);
    } catch (error) {
      this.statusBarHeight = 0;
    }

    this.source = options && options.source ? options.source : 'profile';
    this.pendingProfile = getPendingProfile();

    if (!this.pendingProfile) {
      uni.reLaunch({
        url: '/pages/index/index'
      });
      return;
    }

    if (this.source === 'profile') {
      const result = startBleSession(this.pendingProfile, {
        autoConnect: true
      });

      this.bridgeState = getBridgeState();
      if (!result.ok) {
        this.connectError = result.error;
      }
    }

    this.startConnectFlow();
  },
  onUnload() {
    clearInterval(this.connectTicker);
    this.connectTicker = null;
  },
  methods: {
    requestInitialSnapshot() {
      sendJsonCommand(createGetCapabilitiesCommand());
      sendJsonCommand(createGetConfigCommand());
      sendJsonCommand(createGetStatusCommand());
      sendJsonCommand(createGetCalibrationCommand());
      sendJsonCommand(createQueryLinearSteeringCommand());
    },
    hasHandshakeReply() {
      return !!(
        this.bridgeState.lastJsonAt ||
        this.bridgeState.configAt ||
        this.bridgeState.statusAt ||
        this.bridgeState.chassisStatusAt
      );
    },
    sendNeutralFrame() {
      const frame = createControlFrame({
        seq: this.frameSeq,
        gear: 'N',
        throttlePercent: 0,
        steerPercent: 0,
        auxXPercent: 0,
        auxYPercent: 0,
        buttons: 0
      });
      const result = sendControlFrame(frame);
      if (result.ok) {
        this.frameSeq += 1;
      }
    },
    startConnectFlow() {
      clearInterval(this.connectTicker);
      this.connectStartedAt = Date.now();
      this.connectPhase = 0;
      this.snapshotRequestSent = false;

      this.connectTicker = setInterval(() => {
        this.bridgeState = getBridgeState();

        if (this.bridgeState.lastError) {
          this.connectError = this.bridgeState.lastError;
        }

        if (this.connectPhase === 0 && this.bridgeState.adapterReady) {
          this.progress = 24;
          this.activeStep = 1;
          this.connectPhase = 1;
        }

        if (this.connectPhase <= 1 && this.bridgeState.candidateFound) {
          this.progress = 48;
          this.activeStep = 2;
          this.connectPhase = 2;
        }

        if (this.connectPhase <= 2 && this.bridgeState.clientConnected) {
          this.progress = 70;
          this.activeStep = 3;
          this.connectPhase = 3;
        }

        if (this.connectPhase <= 3 && this.bridgeState.notifyReady) {
          this.progress = 88;
          this.activeStep = 4;
          this.connectPhase = 4;

          if (!this.snapshotRequestSent) {
            this.sendNeutralFrame();
            this.requestInitialSnapshot();
            this.snapshotRequestSent = true;
          }
        }

        if (this.connectPhase <= 4 && this.hasHandshakeReply()) {
          this.progress = 100;
          clearInterval(this.connectTicker);
          this.connectTicker = null;

          const resolvedConfig = this.bridgeState.configData || {
            frontTrackMm: this.pendingProfile.frontTrackMm,
            wheelbaseMm: this.pendingProfile.wheelbaseMm,
            rearTrackMm: this.pendingProfile.rearTrackMm,
            driveMaxRpm: this.pendingProfile.driveMaxRpm,
            chassisType: this.pendingProfile.chassisType,
            driveAxle: this.pendingProfile.driveAxle,
            hasLinearSteering: !!this.pendingProfile.hasLinearSteering,
            pedalConfig: this.pendingProfile.pedalConfig || 'brake_throttle'
          };

          syncSessionFromBridgeState(this.bridgeState);
          setConnectedProfile(this.pendingProfile);
          updateAppConfig(resolvedConfig);
          persistConnectedProfile(this.bridgeState);

          setTimeout(() => {
            uni.reLaunch({
              url: '/pages/dashboard/index'
            });
          }, 320);
        }

        if ((Date.now() - this.connectStartedAt) > 20000 &&
            !this.bridgeState.candidateFound &&
            !this.connectError) {
          this.connectError = '20 秒内未扫描到匹配的 EWM22 BLE 广播，请检查模块已上电并开启广播。';
        }

        if ((Date.now() - this.connectStartedAt) > 45000 &&
            !this.hasHandshakeReply() &&
            !this.connectError) {
          this.connectError = '45 秒内仍未收到 STM32 配置或状态回包，请检查 EWM22、USART1、CAN 与固件。';
        }
      }, 200);
    },
    cancelConnect() {
      clearInterval(this.connectTicker);
      this.connectTicker = null;
      stopBleSession();

      if (this.source === 'pairing') {
        uni.navigateBack({
          delta: 1
        });
        return;
      }

      uni.reLaunch({
        url: '/pages/index/index'
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
  padding: 8px 16px 32px;
  box-sizing: border-box;
}

.center-title-block {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 14px 6px 22px;
}

.center-title {
  font-size: 26px;
  font-weight: 700;
  color: #303133;
}

.center-subtitle {
  margin-top: 8px;
  font-size: 13px;
  color: #909399;
  text-align: center;
}

.panel-card {
  overflow: hidden;
  border: 1px solid #ebeef5;
  border-radius: 16px;
  background: #ffffff;
  box-shadow: 0 8px 24px rgba(16, 24, 40, 0.04);
}

.connect-card {
  padding: 22px 18px 18px;
}

.progress-copy-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.progress-copy-label {
  font-size: 14px;
  color: #606266;
}

.progress-copy-value {
  font-size: 14px;
  font-weight: 600;
  color: #409eff;
}

.progress-track {
  margin-top: 10px;
  height: 8px;
  border-radius: 999px;
  background: #ebeef5;
  overflow: hidden;
}

.progress-bar {
  height: 100%;
  border-radius: 999px;
  background: linear-gradient(90deg, #409eff, #66b1ff);
}

.steps-list {
  margin-top: 24px;
}

.step-row {
  position: relative;
  min-height: 68px;
  display: flex;
  align-items: flex-start;
}

.step-line {
  position: absolute;
  left: 11px;
  top: 26px;
  bottom: -10px;
  width: 2px;
  background: #ebeef5;
}

.step-line.active {
  background: #409eff;
}

.step-icon-wrap {
  position: relative;
  z-index: 2;
  margin-top: 2px;
}

.step-icon {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
}

.step-done {
  background: #409eff;
}

.step-current {
  border: 2px solid #409eff;
  background: #ffffff;
}

.step-pending {
  border: 2px solid #c0c4cc;
  background: #ffffff;
}

.step-pulse {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #409eff;
  animation: pulse 1s infinite;
}

.step-copy {
  margin-left: 14px;
  padding-bottom: 18px;
}

.step-title {
  display: block;
  font-size: 14px;
  font-weight: 600;
  color: #303133;
}

.step-title.inactive {
  color: #c0c4cc;
}

.step-desc {
  display: block;
  margin-top: 5px;
  font-size: 12px;
  line-height: 18px;
  color: #909399;
}

.step-desc.current {
  color: #409eff;
}

.error-panel {
  margin-top: 14px;
  padding: 12px 14px;
  border-radius: 12px;
  border: 1px solid #fde2e2;
  background: #fef0f0;
  display: flex;
  align-items: center;
}

.error-text {
  margin-left: 8px;
  font-size: 12px;
  line-height: 18px;
  color: #f56c6c;
}

.footer-row {
  padding: 18px 0 0;
}

.ghost-button {
  height: 46px;
  border-radius: 14px;
  border: 1px solid #dcdfe6;
  background: #ffffff;
  display: flex;
  align-items: center;
  justify-content: center;
}

.ghost-button-text {
  font-size: 14px;
  color: #606266;
}

@keyframes pulse {
  0% {
    transform: scale(0.9);
    opacity: 0.7;
  }

  70% {
    transform: scale(1.3);
    opacity: 0;
  }

  100% {
    transform: scale(1.3);
    opacity: 0;
  }
}
</style>
