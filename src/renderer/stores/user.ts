import { defineStore } from 'pinia';

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
      // 注意：data 可能是 login 接口返回，也可能是 /user/detail 返回
      const user: UserInfo = {
        userid: data.userid || data.userId || this.info?.userid,
        nickname: data.nickname || data.userName || this.info?.nickname,
        pic: data.pic || data.userPic || this.info?.pic,
        token: data.token || this.info?.token, // 详情接口可能不返回 token，保留旧的
        t1: data.t1 || this.info?.t1,
        vip_type: data.vip_type !== undefined ? data.vip_type : this.info?.vip_type,
        p_grade: data.p_grade || data.extendsInfo?.detail?.p_grade || this.info?.p_grade,
        extendsInfo: {
          detail: data.extendsInfo?.detail || data.detail || this.info?.extendsInfo?.detail || {},
          vip: data.extendsInfo?.vip || data.vip || this.info?.extendsInfo?.vip || {}
        }
      };
      this.setUserInfo(user);
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
