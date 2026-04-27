<template>
  <view class="page-shell">
    <view class="safe-top" :style="{ height: `${statusBarHeight}px` }"></view>

    <view class="page-body page-narrow">
      <view class="hero-block">
        <view class="hero-icon">
          <AppSvgIcon name="bluetooth" :size="34" color="#ffffff" />
        </view>
        <text class="hero-title">线控底盘终端</text>
        <text class="hero-subtitle">连接 EWM22 BLE 模块，进入 F407 底盘闭环调试</text>
      </view>

      <view v-if="bootError" class="error-panel">
        <AppSvgIcon name="bluetooth-off" :size="18" color="#F56C6C" />
        <text class="error-text">{{ bootError }}</text>
      </view>

      <view class="action-card action-card-primary" @tap="goCreateProfile">
        <text class="action-card-title">新建底盘档案</text>
        <text class="action-card-subtitle">扫描附近蓝牙模块并创建本地档案</text>
      </view>

      <view class="panel-card">
        <view class="card-head">
          <text class="card-head-title">本地档案 ({{ profiles.length }})</text>
        </view>

        <view class="profile-list">
          <view v-if="!isLoadingProfiles && profiles.length === 0" class="empty-panel">
            <text class="empty-title">当前还没有本地档案</text>
            <text class="empty-tip">先新建档案，完成一次配对后，下次可以直接搜索并连接。</text>
          </view>

          <view v-if="isLoadingProfiles && profiles.length === 0" class="empty-panel">
            <AppSvgIcon name="loader" :size="24" color="#409EFF" :spin="true" />
            <text class="empty-title empty-gap">正在加载本地档案...</text>
          </view>

          <view
            v-for="profile in profiles"
            :key="profile.id"
            class="profile-row"
            @tap="beginConnect(profile)"
          >
            <view class="profile-main">
              <view class="profile-badge">
                <AppSvgIcon name="cpu" :size="22" color="#409EFF" />
              </view>

              <view class="profile-copy">
                <view class="profile-title-line">
                  <text class="profile-title">{{ profile.name }}</text>
                  <view class="tag-chip tag-success">
                    <text class="tag-chip-text">已保存</text>
                  </view>
                </view>
                <text class="profile-code">档案号: {{ profile.chassisCode }}</text>
                <text class="profile-line">蓝牙名: {{ profile.bleNameHint || '--' }}</text>
                <text class="profile-line">结构: {{ profile.chassisType === 'diff' ? '线性差速' : '线性阿克曼' }} / {{ formatDriveAxle(profile.driveAxle) }}</text>
                <text class="profile-line">{{ profile.lastConnectedAt ? `最近连接: ${formatTime(profile.lastConnectedAt)}` : '还没有成功连接记录' }}</text>
              </view>
            </view>

            <AppSvgIcon name="chevron" :size="18" color="#C0C4CC" />
          </view>
        </view>
      </view>
    </view>
  </view>
</template>

<script>
import AppSvgIcon from '@/components/AppSvgIcon.vue';
import { getAppConfig } from '@/utils/session-state';
import {
  loadProfilesFromStorage,
  resetSessionState,
  setPendingProfile
} from '@/utils/session-state';
import { stopBleSession } from '@/utils/ewm22-ble-bridge';

export default {
  components: {
    AppSvgIcon
  },
  data() {
    return {
      statusBarHeight: 0,
      profiles: [],
      isLoadingProfiles: false,
      bootError: ''
    };
  },
  onShow() {
    this.initPage();
  },
  methods: {
    initPage() {
      try {
        const systemInfo = uni.getSystemInfoSync();
        this.statusBarHeight = Number(systemInfo.statusBarHeight || 0);
      } catch (error) {
        this.statusBarHeight = 0;
      }

      stopBleSession();
      resetSessionState();
      this.loadProfiles();
    },
    loadProfiles() {
      this.isLoadingProfiles = true;
      this.profiles = loadProfilesFromStorage();
      this.isLoadingProfiles = false;
    },
    goCreateProfile() {
      uni.navigateTo({
        url: '/pages/pairing/index'
      });
    },
    beginConnect(profile) {
      const appConfig = getAppConfig();
      setPendingProfile({
        ...profile,
        frontTrackMm: profile.frontTrackMm || appConfig.frontTrackMm,
        wheelbaseMm: profile.wheelbaseMm || appConfig.wheelbaseMm,
        rearTrackMm: profile.rearTrackMm || appConfig.rearTrackMm,
        driveMaxRpm: profile.driveMaxRpm || appConfig.driveMaxRpm,
        chassisType: profile.chassisType || appConfig.chassisType,
        driveAxle: profile.driveAxle || appConfig.driveAxle,
        hasLinearSteering: typeof profile.hasLinearSteering === 'boolean' ? profile.hasLinearSteering : appConfig.hasLinearSteering,
        pedalConfig: profile.pedalConfig || appConfig.pedalConfig
      });

      uni.navigateTo({
        url: '/pages/connecting/index?source=profile'
      });
    },
    formatTime(timestamp) {
      if (!timestamp) {
        return '--';
      }

      const target = new Date(timestamp);
      const month = `${target.getMonth() + 1}`.padStart(2, '0');
      const day = `${target.getDate()}`.padStart(2, '0');
      const hour = `${target.getHours()}`.padStart(2, '0');
      const minute = `${target.getMinutes()}`.padStart(2, '0');
      return `${month}-${day} ${hour}:${minute}`;
    },
    formatDriveAxle(value) {
      return String(value || '').toLowerCase() === 'fwd' ? '前驱' : '后驱';
    }
  }
};
</script>

<style lang="scss" scoped>
.page-shell {
  min-height: 100vh;
  background: #f2f6fc;
  color: #303133;
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

.hero-block {
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 18px 8px 28px;
}

.hero-icon {
  width: 64px;
  height: 64px;
  border-radius: 18px;
  background: linear-gradient(135deg, #409eff, #2d8cf0);
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 12px 30px rgba(64, 158, 255, 0.24);
}

.hero-title {
  margin-top: 16px;
  font-size: 28px;
  font-weight: 700;
}

.hero-subtitle {
  margin-top: 8px;
  font-size: 13px;
  line-height: 20px;
  color: #909399;
  text-align: center;
}

.action-card {
  margin-bottom: 16px;
  padding: 18px;
  border-radius: 16px;
}

.action-card-primary {
  background: linear-gradient(135deg, #409eff, #2d8cf0);
  box-shadow: 0 12px 24px rgba(64, 158, 255, 0.18);
}

.action-card-title {
  display: block;
  font-size: 17px;
  font-weight: 700;
  color: #ffffff;
}

.action-card-subtitle {
  display: block;
  margin-top: 6px;
  font-size: 12px;
  color: rgba(255, 255, 255, 0.88);
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
  justify-content: center;
}

.card-head-title {
  font-size: 14px;
  font-weight: 600;
  color: #606266;
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
}

.tag-chip-text {
  font-size: 11px;
  color: #67c23a;
}

.tag-success {
  background: #f0f9eb;
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

.empty-gap {
  margin-top: 10px;
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
</style>
