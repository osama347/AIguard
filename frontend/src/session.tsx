import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from "react";
import { notify, requestNotifications } from "./lib/notify";
import { api, getToken, onAuthChange, setToken, subscribe, type Alert, type Community, type Health, type User } from "./api/client";

export interface Notice {
  id: number;
  message: string;
  tone: "ok" | "info";
}

interface Session {
  user: User | null;
  isAdmin: boolean;
  health: Health | null;
  community: Community | null;
  healthError: Error | null;
  openAlerts: number;
  toasts: Alert[];
  notices: Notice[];
  refreshHealth: () => Promise<void>;
  refreshCommunity: () => Promise<void>;
  refreshAlerts: () => void;
  signIn: (token: string, user: User) => void;
  signOut: () => Promise<void>;
  dismissToast: (id: number) => void;
  pushNotice: (message: string, tone?: Notice["tone"]) => void;
  dismissNotice: (id: number) => void;
}

const Ctx = createContext<Session | null>(null);
export const useSession = () => useContext(Ctx)!;

export function SessionProvider({ children }: { children: ReactNode }) {
  const [token, setTok] = useState(getToken());
  const [user, setUser] = useState<User | null>(null);
  const [health, setHealth] = useState<Health | null>(null);
  const [healthError, setHealthError] = useState<Error | null>(null);
  const [community, setCommunity] = useState<Community | null>(null);
  const [openAlerts, setOpenAlerts] = useState(0);
  const [toasts, setToasts] = useState<Alert[]>([]);
  const [notices, setNotices] = useState<Notice[]>([]);
  const noticeId = useRef(0);

  useEffect(() => onAuthChange(() => setTok(getToken())), []);

  const refreshHealth = useCallback(async () => {
    try {
      setHealth(await api.health());
      setHealthError(null);
    } catch (e) {
      setHealthError(e as Error);
    }
  }, []);

  const refreshCommunity = useCallback(async () => {
    try { setCommunity(await api.community()); } catch { /* branding is optional */ }
  }, []);

  // Community name and logo are public, so the login screen can show them.
  const serverReachable = health !== null;
  useEffect(() => { if (serverReachable) refreshCommunity(); }, [serverReachable, refreshCommunity]);

  // Keep branding fresh: when an admin changes the community name or logo the sidebars
  // and login screen pick it up without a reload (the community endpoint is public + tiny).
  const poll = useCallback(async () => {
    await refreshHealth();
    refreshCommunity();
  }, [refreshHealth, refreshCommunity]);

  useEffect(() => {
    refreshHealth();
    const t = setInterval(poll, 10000);
    return () => clearInterval(t);
  }, [poll]);

  useEffect(() => {
    if (!token) { setUser(null); return; }
    api.me().then(setUser).catch(() => setUser(null));
  }, [token]);

  const refreshAlerts = useCallback(() => {
    if (!getToken()) return;
    api.alerts(true).then((a) => setOpenAlerts(a.length)).catch(() => {});
  }, []);

  // Global live feed: alert badge, toasts and OS notifications.
  useEffect(() => {
    if (!user) return;
    refreshAlerts();
    return subscribe((ev) => {
      if (ev.type === "alert.created") {
        setToasts((t) => [ev.data, ...t].slice(0, 4));
        refreshAlerts();
        notify(ev.data.severity === "critical" ? "Guard++ critical alert" : "Guard++ alert", ev.data.message);
      } else if (ev.type === "alert.updated") {
        refreshAlerts();
      }
    });
  }, [user, refreshAlerts]);

  const signIn = (t: string, u: User) => {
    setToken(t);
    setUser(u);
    refreshCommunity();
    requestNotifications();
  };

  const signOut = async () => {
    try { await api.logout(); } catch { /* token may already be invalid */ }
    setToken(null);
  };

  const dismissToast = (id: number) => setToasts((t) => t.filter((a) => a.id !== id));

  // User-action confirmations ("Driver saved", "Vehicle registered"…), the opposite of
  // the alert toasts above: something the user just did, not something the system saw.
  const pushNotice = useCallback((message: string, tone: Notice["tone"] = "ok") => {
    const id = ++noticeId.current;
    setNotices((n) => [...n, { id, message, tone }].slice(-4));
    window.setTimeout(() => setNotices((n) => n.filter((x) => x.id !== id)), 4000);
  }, []);
  const dismissNotice = useCallback((id: number) => setNotices((n) => n.filter((x) => x.id !== id)), []);

  return (
    <Ctx.Provider value={{ user: token ? user : null, isAdmin: !!token && user?.role === "admin", health, community, healthError, openAlerts, toasts, notices,
                           refreshHealth, refreshCommunity, refreshAlerts, signIn, signOut, dismissToast, pushNotice, dismissNotice }}>
      {children}
    </Ctx.Provider>
  );
}
