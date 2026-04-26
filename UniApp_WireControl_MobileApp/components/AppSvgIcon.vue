<template>
  <view class="app-svg-icon" :class="{ 'is-spin': spin }" :style="wrapperStyle">
    <image class="app-svg-icon-image" :style="imageStyle" :src="iconSrc" mode="aspectFit" />
  </view>
</template>

<script>
import { buildIconDataUri } from '../utils/icon-map';

export default {
  name: 'AppSvgIcon',
  props: {
    name: {
      type: String,
      default: 'radio'
    },
    size: {
      type: Number,
      default: 24
    },
    color: {
      type: String,
      default: '#303133'
    },
    spin: {
      type: Boolean,
      default: false
    }
  },
  computed: {
    wrapperStyle() {
      return {
        width: `${this.size}px`,
        height: `${this.size}px`
      };
    },
    imageStyle() {
      return {
        width: `${this.size}px`,
        height: `${this.size}px`
      };
    },
    iconSrc() {
      return buildIconDataUri(this.name, this.color, Math.max(1.6, this.size / 14));
    }
  }
};
</script>

<style scoped>
.app-svg-icon {
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.app-svg-icon-image {
  display: block;
}

.is-spin {
  animation: spin 0.9s linear infinite;
}

@keyframes spin {
  0% {
    transform: rotate(0deg);
  }

  100% {
    transform: rotate(360deg);
  }
}
</style>
