<template>
  <view class="page-shell">
    <view class="safe-top" :style="{ height: `${statusBarHeight}px` }"></view>

    <view class="page-body page-narrow">
      <view class="center-title-block">
        <text class="center-title">选择蓝牙设备</text>
        <text class="center-subtitle">新建档案时先选中 infinite-robot-001 BLE 模块</text>
      </view>

      <view class="panel-card">
        <view class="card-head card-head-between">
          <text class="card-head-inline">附近设备 ({{ discoveredDevices.length }})</text>
          <view class="card-head-action" :class="{ disabled: bridgeState.discovering }" @tap="restartPairingScan">
            <AppSvgIcon
              :name="bridgeState.discovering ? 'loader' : 'bluetooth'"
              :size="16"
              color="#409EFF"
              :spin="bridgeState.discovering"
            />
            <text class="card-head-action-text">{{ bridgeState.discovering ? '扫描中' : '重扫' }}</text>
          </view>
        </view>

        <view class="profile-list">
          <view v-if="discoveredDevices.length === 0" class="empty-panel">
            <AppSvgIcon v-if="bridgeState.discovering" name="loader" :size="24" color="#409EFF" :spin="true" />
            <text class="empty-title">{{ bridgeState.discovering ? '正在搜索附近设备...' : '还没有发现可用设备' }}</text>
            <text class="empty-tip">打开 infinite-robot-001 广播后，再点右上角重新扫描。</text>
          </view>

          <view
            v-for="device in discoveredDevices"
            :key="device.deviceId"
            class="profile-row"
            @tap="selectPairingDevice(device)"
          >
            <view class="profile-main">
              <view class="profile-badge">
                <AppSvgIcon name="bluetooth" :size="22" color="#409EFF" />
              </view>

              <view class="profile-copy">
                <view class="profile-title-line">
                  <text class="profile-title">{{ device.name }}</text>
                  <view class="tag-chip tag-info">
                    <text class="tag-chip-text">可连接</text>
                  </view>
                </view>
                <text class="profile-code">设备ID: {{ device.deviceId }}</text>
                <text class="profile-line">{{ formatRssi(device.rssi) }}</text>
              </view>
            </view>

            <AppSvgIcon name="chevron" :size="18" color="#C0C4CC" />
          </view>
        </view>
      </view>

      <view v-if="connectError" class="error-panel">
        <AppSvgIcon name="bluetooth-off" :size="18" color="#F56C6C" />
        <text class="error-text">{{ connectError }}</text>
      </view>

      <view class="footer-row">
        <view class="ghost-button" @tap="cancelPairing">
          <text class="ghost-button-text">返回首页</text>
        </view>
      </view>
    </view>
  </view>
</template>

<script>
import AppSvgIcon from '@/components/AppSvgIcon.vue';
import { PROFILE_TEMPLATE, createProfileFromBleDevice } from '@/utils/device-profiles';
import { setPendingProfile } from '@/utils/session-state';
import { connectBleDevice, getBridgeState, startBleSession, stopBleSession } from '@/utils/ewm22-ble-bridge';

export default {
  components: {
    AppSvgIcon
  },
  data() {
    return {
      statusBarHeight: 0,
      bridgeState: getBridgeState(),
      connectError: '',
      bridgeTicker: null
    };
  },
  computed: {
    discoveredDevices() {
      return this.bridgeState.discoveredDevices || [];
    }
  },
  onLoad() {
    try {
      const systemInfo = uni.getSystemInfoSync();
      this.statusBarHeight = Number(systemInfo.statusBarHeight || 0);
    } catch (error) {
      this.statusBarHeight = 0;
    }

    this.startPairingScan();
    this.startBridgeTicker();
  },
  onUnload() {
    clearInterval(this.bridgeTicker);
    this.bridgeTicker = null;
  },
  methods: {
    startBridgeTicker() {
      clearInterval(this.bridgeTicker);
      this.bridgeTicker = setInterval(() => {
        this.bridgeState = getBridgeState();
      }, 150);
    },
    startPairingScan() {
      this.connectError = '';
      stopBleSession();
      this.bridgeState = getBridgeState();

      const result = startBleSession(PROFILE_TEMPLATE, {
        autoConnect: false
      });

      this.bridgeState = getBridgeState();
      if (!result.ok) {
        this.connectError = result.error;
      }
    },
    restartPairingScan() {
      this.startPairingScan();
    },
    selectPairingDevice(device) {
      const nextProfile = createProfileFromBleDevice(device.name);
      const result = connectBleDevice(device.deviceId);

      if (!result.ok) {
        this.connectError = result.error;
        return;
      }

      setPendingProfile(nextProfile);
      uni.navigateTo({
        url: '/pages/connecting/index?source=pairing'
      });
    },
    cancelPairing() {
      stopBleSession();
      uni.navigateBack({
        delta: 1
      });
    },
    formatRssi(rssi) {
      if (typeof rssi !== 'number') {
        return '信号强度: --';
      }

      return `信号强度: ${rssi} dBm`;
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

.card-head-inline {
  font-size: 14px;
  font-weight: 600;
  color: #606266;
}

.card-head-action {
  display: flex;
  align-items: center;
}

.card-head-action.disabled {
  opacity: 0.55;
}

.card-head-action-text {
  margin-left: 4px;
  font-size: 12px;
  color: #409eff;
}

.profile-list {
  background: #ffffff;
}

.profile-row {
  padding: 16px;
  border-bottom: 1px solid #ebeef5;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.profile-row:last-child {
  border-bottom: none;
}

.profile-main {
  display: flex;
  align-items: flex-start;
  flex: 1;
  min-width: 0;
}

.profile-badge {
  width: 42px;
  height: 42px;
  border-radius: 12px;
  background: #ecf5ff;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-right: 12px;
  flex-shrink: 0;
}

.profile-copy {
  min-width: 0;
  flex: 1;
}

.profile-title-line {
  display: flex;
  align-items: center;
  gap: 8px;
}

.profile-title {
  font-size: 15px;
  font-weight: 600;
  color: #303133;
}

.profile-code,
.profile-line {
  display: block;
  margin-top: 5px;
  font-size: 12px;
  line-height: 18px;
  color: #909399;
  word-break: break-all;
}

.tag-chip {
  padding: 3px 8px;
  border-radius: 999px;
  background: #ecf5ff;
}

.tag-chip-text {
  font-size: 11px;
  color: #409eff;
}

.empty-panel {
  padding: 28px 16px;
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
}

.empty-title {
  margin-top: 8px;
  font-size: 14px;
  color: #606266;
}

.empty-tip {
  margin-top: 6px;
  font-size: 12px;
  line-height: 18px;
  color: #909399;
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
</style>
