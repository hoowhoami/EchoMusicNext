import { defineStore } from 'pinia';
import { getUserDetail, getUserVipDetail } from '@/api/user';
import logger from '@/utils/logger';

export interface UserInfo {
  userid?: number;
  nickname?: string;
  pic?: string;
  token?: string;
  t1?: string;
  vip_type?: number;
  p_grade?: number;
  extendsInfo?: {
    detail?: Record<string, unknown>;
    vip?: Record<string, unknown>;
    [key: string]: unknown;
  };
}

interface ApiPayload {
  status?: number;
  data?: unknown;
  [key: string]: unknown;
}

const readNumber = (value: unknown): number | undefined =>
  typeof value === 'number' ? value : undefined;

const asApiPayload = (value: unknown): ApiPayload | null => {
  if (!value || typeof value !== 'object') return null;
  return value as ApiPayload;
};

export const useUserStore = defineStore('user', {
  state: () => ({
    info: null as UserInfo | null,
    isLoggedIn: false,
    hasFetchedUserInfo: false,
    isFetchingUserInfo: false,
    // 每日权益状态
    isTvipClaimedToday: false,
    isSvipClaimedToday: false,
  }),
  actions: {
    setUserInfo(info: UserInfo) {
      this.info = info;
      this.isLoggedIn = !!info.token;
      if (!info.token) {
        this.hasFetchedUserInfo = false;
      }
    },
    handleLoginSuccess(data: Record<string, unknown>) {
      this.hasFetchedUserInfo = false;
      // 兼容多种返回格式
      const rawExtends =
        (data.extends as UserInfo['extendsInfo'] | undefined) ||
        (data.extendsInfo as UserInfo['extendsInfo'] | undefined) ||
        this.info?.extendsInfo;

      const user: UserInfo = {
        userid:
          typeof data.userid === 'number'
            ? data.userid
            : typeof data.userId === 'number'
              ? data.userId
              : this.info?.userid,
        nickname:
          typeof data.nickname === 'string'
            ? data.nickname
            : typeof data.userName === 'string'
              ? data.userName
              : this.info?.nickname,
        pic:
          typeof data.pic === 'string'
            ? data.pic
            : typeof data.userPic === 'string'
              ? data.userPic
              : this.info?.pic,
        token: typeof data.token === 'string' ? data.token : this.info?.token,
        t1: typeof data.t1 === 'string' ? data.t1 : this.info?.t1,
        vip_type: typeof data.vip_type === 'number' ? data.vip_type : this.info?.vip_type,
        p_grade:
          typeof data.p_grade === 'number'
            ? data.p_grade
            : readNumber(rawExtends?.detail?.p_grade) ||
              readNumber((data.detail as Record<string, unknown> | undefined)?.p_grade) ||
              this.info?.p_grade,
        extendsInfo: {
          detail:
            rawExtends?.detail ||
            ((data.detail as Record<string, unknown> | undefined) ?? this.info?.extendsInfo?.detail ?? {}),
          vip:
            rawExtends?.vip ||
            ((data.vip as Record<string, unknown> | undefined) ?? this.info?.extendsInfo?.vip ?? {}),
        },
      };
      this.setUserInfo(user);
    },
    /**
     * 主动获取用户详情 (等级、VIP等)
     */
    async fetchUserInfo() {
      if (!this.isLoggedIn) return;
      try {
        const [detailRes, vipRes] = await Promise.all([getUserDetail(), getUserVipDetail()]);
        const detailPayload = asApiPayload(detailRes);
        const vipPayload = asApiPayload(vipRes);

        if (detailPayload?.status === 1) {
          logger.info('UserStore', 'User detail fetched');
          const payload =
            detailPayload.data && typeof detailPayload.data === 'object'
              ? (detailPayload.data as Record<string, unknown>)
              : detailPayload;
          this.handleLoginSuccess(payload);
        }

        if (vipPayload?.status === 1 && this.info) {
          logger.info('UserStore', 'VIP detail fetched');
          const currentExtends = this.info?.extendsInfo || {};
          this.info = {
            ...this.info,
            extendsInfo: {
              ...currentExtends,
              vip: vipPayload.data && typeof vipPayload.data === 'object'
                ? (vipPayload.data as Record<string, unknown>)
                : vipPayload,
            },
          };
        }
      } catch (e) {
        logger.error('UserStore', 'Fetch user info error:', e);
      }
    },
    async fetchUserInfoOnce() {
      if (!this.isLoggedIn || this.hasFetchedUserInfo || this.isFetchingUserInfo) return;
      this.isFetchingUserInfo = true;
      try {
        await this.fetchUserInfo();
        this.hasFetchedUserInfo = true;
      } finally {
        this.isFetchingUserInfo = false;
      }
    },
    setClaimStatus(tvip: boolean, svip: boolean) {
      this.isTvipClaimedToday = tvip;
      this.isSvipClaimedToday = svip;
    },
    logout() {
      this.info = null;
      this.isLoggedIn = false;
      this.hasFetchedUserInfo = false;
      this.isFetchingUserInfo = false;
      this.isTvipClaimedToday = false;
      this.isSvipClaimedToday = false;
    },
  },
  persist: {
    omit: ['hasFetchedUserInfo', 'isFetchingUserInfo'],
  },
});
