// OS notifications: native (Tauri plugin) inside the desktop app, the Web
// Notification API in browsers.
interface TauriNotification {
  isPermissionGranted(): Promise<boolean>;
  requestPermission(): Promise<string>;
  sendNotification(opts: { title: string; body?: string }): void;
}

const tauri = (): TauriNotification | undefined =>
  (window as unknown as { __TAURI__?: { notification?: TauriNotification } }).__TAURI__?.notification;

export async function notificationsEnabled(): Promise<boolean> {
  const t = tauri();
  if (t) return t.isPermissionGranted();
  return typeof Notification !== "undefined" && Notification.permission === "granted";
}

export async function requestNotifications(): Promise<boolean> {
  const t = tauri();
  if (t) return (await t.isPermissionGranted()) || (await t.requestPermission()) === "granted";
  if (typeof Notification === "undefined") return false;
  return (await Notification.requestPermission()) === "granted";
}

export async function notify(title: string, body: string) {
  if (!(await notificationsEnabled())) return;
  const t = tauri();
  if (t) t.sendNotification({ title, body });
  else new Notification(title, { body });
}
