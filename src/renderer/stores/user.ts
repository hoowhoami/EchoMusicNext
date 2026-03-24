import { defineStore } from 'pinia';
import { getUserDetail, getUserVipDetail } from '../api/user';
import logger from '../utils/logger';

export interface UserInfo {
  userid?: number;
  nickname?: string;
  pic?: string;
  token?: string;
  t1?: string;
  vip_type?: number;
  p_grade?: number;
  extendsInfo?: {
    detail?: any;
    vip?: any;
    [key: string]: any;
  };
}

export const useUserStore = defineStore('user', {
  state: () => ({
    info: null as UserInfo | null,
    isLoggedIn: false,
    // 每日权益状态 (1:1 复刻)
    isTvipClaimedToday: false,
    isSvipClaimedToday: false,
  }),
  actions: {
    setUserInfo(info: UserInfo) {
      this.info = info;
      this.isLoggedIn = !!info.token;
    },
    handleLoginSuccess(data: any) {
      // 兼容多种返回格式 (酷狗概念版 API 特色)
      const rawExtends = data.extends || data.extendsInfo || this.info?.extendsInfo;
      
      const user: UserInfo = {
        userid: data.userid || data.userId || this.info?.userid,
        nickname: data.nickname || data.userName || this.info?.nickname,
        pic: data.pic || data.userPic || this.info?.pic,
        token: data.token || this.info?.token,
        t1: data.t1 || this.info?.t1,
        vip_type: data.vip_type !== undefined ? data.vip_type : this.info?.vip_type,
        p_grade: data.p_grade || rawExtends?.detail?.p_grade || data.detail?.p_grade || this.info?.p_grade,
        extendsInfo: {
          detail: rawExtends?.detail || data.detail || this.info?.extendsInfo?.detail || {},
          vip: rawExtends?.vip || data.vip || this.info?.extendsInfo?.vip || {}
        }
      };
      this.setUserInfo(user);
    },
    /**
     * 主动获取用户详情 (等级、VIP等)
     */
    async fetchUserInfo() {
      if (!this.isLoggedIn) return;
      try {
        const [detailRes, vipRes]: any = await Promise.all([
          getUserDetail(),
          getUserVipDetail()
        ]);

        if (detailRes && detailRes.status === 1) {
          logger.info('[UserStore] User detail fetched');
          this.handleLoginSuccess(detailRes.data || detailRes);
        }

        if (vipRes && vipRes.status === 1) {
          logger.info('[UserStore] VIP detail fetched');
          const currentExtends = this.info?.extendsInfo || {};
          this.info = {
            ...this.info!,
            extendsInfo: {
              ...currentExtends,
              vip: vipRes.data || vipRes
            }
          };
        }
      } catch (e) {
        logger.error('[UserStore] Fetch user info error:', e);
      }
    },
    setClaimStatus(tvip: boolean, svip: boolean) {
      this.isTvipClaimedToday = tvip;
      this.isSvipClaimedToday = svip;
    },
    logout() {
      this.info = null;
      this.isLoggedIn = false;
      this.isTvipClaimedToday = false;
      this.isSvipClaimedToday = false;
    }
  },
  persist: true,
});
