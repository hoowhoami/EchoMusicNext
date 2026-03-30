export interface PlaylistInfo {
  id: string;
  name: string;
  coverUrl: string;
  songCount: number;
  userId: number;
  isDefault?: boolean;
  type?: number;
  source?: number;
}
