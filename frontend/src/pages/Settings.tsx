import { useEffect, useState } from "react";
import { notificationsEnabled, requestNotifications } from "../lib/notify";
import { api, assetUrl, isDesktop, serverUrl, setServerUrl, type CommunityInput } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { formatBytes } from "../lib/format";
import { Badge, ErrorBox, Field, PageHeader, Spinner } from "../components/ui";

export function SettingsPage() {
  const { user, isAdmin, health, community, refreshCommunity, signOut } = useSession();
  const system = useAsync(() => api.system(), []);
  const [url, setUrl] = useState(serverUrl());
  const [form, setForm] = useState<CommunityInput>({});
  const [comMsg, setComMsg] = useState<string | null>(null);
  const [comErr, setComErr] = useState<string | null>(null);
  useEffect(() => {
    if (community) setForm({ name: community.name, address: community.address, city: community.city, country: community.country,
                             helpline: community.helpline, email: community.email, website: community.website });
  }, [community]);
  const setF = (k: keyof CommunityInput) => (e: React.ChangeEvent<HTMLInputElement>) => setForm({ ...form, [k]: e.target.value });
  const saveCommunity = async (action: () => Promise<unknown>, ok: string) => {
    setComMsg(null); setComErr(null);
    try { await action(); await refreshCommunity(); setComMsg(ok); } catch (e) { setComErr((e as Error).message); }
  };
  const [notif, setNotif] = useState(true);
  useEffect(() => { notificationsEnabled().then(setNotif); }, []);
  const models = system.data?.models as { state?: string; models?: string[]; trt_version?: string; device?: string; precision?: string } | null | undefined;

  return (
    <>
      <PageHeader title="Settings" />

      {isDesktop && (
        <div className="card">
          <h2>Server</h2>
          <form className="row" onSubmit={(e) => { e.preventDefault(); setServerUrl(url || null); window.location.reload(); }}>
            <Field label="Guard++ server address" hint="The Jetson's address, for example http://192.168.1.20:8090">
              <input value={url} onChange={(e) => setUrl(e.target.value)} placeholder="http://192.168.1.20:8090" />
            </Field>
            <button className="btn">Save & reconnect</button>
          </form>
        </div>
      )}

      {isAdmin && (
        <div className="card">
          <h2>Community</h2>
          <form className="stack" onSubmit={(e) => { e.preventDefault(); void saveCommunity(() => api.saveCommunity(form), "Saved."); }}>
            <ErrorBox error={comErr} />
            {comMsg && <p className="muted">{comMsg}</p>}
            <Field label="Community name"><input value={form.name ?? ""} onChange={setF("name")} required maxLength={120} /></Field>
            <Field label="Address"><input value={form.address ?? ""} onChange={setF("address")} maxLength={300} /></Field>
            <div className="row">
              <Field label="City"><input value={form.city ?? ""} onChange={setF("city")} maxLength={100} /></Field>
              <Field label="Country"><input value={form.country ?? ""} onChange={setF("country")} maxLength={100} /></Field>
            </div>
            <div className="row">
              <Field label="Helpline"><input value={form.helpline ?? ""} onChange={setF("helpline")} maxLength={60} /></Field>
              <Field label="Email"><input type="email" value={form.email ?? ""} onChange={setF("email")} maxLength={120} /></Field>
            </div>
            <Field label="Website"><input value={form.website ?? ""} onChange={setF("website")} maxLength={200} /></Field>
            <div><button className="btn btn-primary">Save community details</button></div>
          </form>
          <div className="row" style={{ marginTop: 14, alignItems: "center" }}>
            {community?.logo_url && <img className="logo-preview" src={assetUrl(community.logo_url)} alt="Community logo" />}
            <Field label="Logo" hint="PNG, JPEG or WebP, up to 2 MB.">
              <input type="file" accept="image/png,image/jpeg,image/webp"
                     onChange={(e) => { const f = e.target.files?.[0]; if (f) void saveCommunity(() => api.uploadLogo(f), "Logo updated."); }} />
            </Field>
            {community?.logo_url && (
              <button className="btn" onClick={() => void saveCommunity(() => api.deleteLogo(), "Logo removed.")}>Remove logo</button>
            )}
          </div>
        </div>
      )}

      <div className="card">
        <h2>Account</h2>
        <p>Signed in as <strong>{user?.full_name || user?.username}</strong>{user?.full_name && <span className="muted"> ({user.username})</span>}{" "}
          <Badge tone="info">{isAdmin ? "Administrator" : "Guard"}</Badge></p>
        <div className="row">
          {!notif && (
            <button className="btn" onClick={() => requestNotifications().then(setNotif)}>Enable alert notifications</button>
          )}
          <button className="btn btn-danger" onClick={signOut}>Sign out</button>
        </div>
      </div>

      <PasswordCard />

      <div className="card">
        <h2>System</h2>
        <ErrorBox error={system.error} onRetry={system.reload} />
        {!system.data ? <Spinner /> : (
          <dl className="facts">
            <dt>Version</dt><dd>{system.data.version}</dd>
            <dt>AI engine</dt>
            <dd>
              <Badge tone={health?.inference.status === "ok" ? "ok" : "warn"}>{health?.inference.status ?? "unknown"}</Badge>
              {health?.inference.message && <span className="muted small"> {health.inference.message}</span>}
            </dd>
            {models?.device && <><dt>Device</dt><dd>{models.device}</dd></>}
            {models?.trt_version && <><dt>TensorRT</dt><dd>{models.trt_version} ({models.precision})</dd></>}
            {models?.models && <><dt>Models</dt><dd>{models.models.join(", ")}</dd></>}
            <dt>Face match threshold</dt><dd>{system.data.policy.match_threshold.toFixed(2)}</dd>
            <dt>Default analysis rate</dt><dd>{system.data.policy.sample_fps} frames/s</dd>
            {system.data.live && <>
              <dt>Cameras</dt><dd>{system.data.live.cameras}</dd>
              <dt>Visit ends after</dt><dd>{system.data.live.visit_gap_s} s with nothing in view</dd>
              <dt>Repeat sightings</dt><dd>merged within {system.data.live.repeat_suppress_s} s</dd>
              <dt>Snapshots kept</dt><dd>{system.data.live.snapshot_retention_days} days (the access log itself is kept)</dd>
            </>}
            {health && <><dt>Free disk</dt><dd>{formatBytes(health.disk.free_bytes)} of {formatBytes(health.disk.total_bytes)}</dd></>}
          </dl>
        )}
      </div>
    </>
  );
}

function PasswordCard() {
  const [current, setCurrent] = useState("");
  const [next, setNext] = useState("");
  const [repeat, setRepeat] = useState("");
  const [msg, setMsg] = useState<{ ok: boolean; text: string } | null>(null);
  const [saving, setSaving] = useState(false);

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    if (next !== repeat) { setMsg({ ok: false, text: "The new passwords do not match." }); return; }
    setSaving(true);
    try {
      await api.changePassword(current, next);
      setMsg({ ok: true, text: "Password changed." });
      setCurrent(""); setNext(""); setRepeat("");
    } catch (err) {
      setMsg({ ok: false, text: (err as Error).message });
    } finally {
      setSaving(false);
    }
  };

  return (
    <form className="card" onSubmit={submit}>
      <h2>Change password</h2>
      <div className="grid-3">
        <Field label="Current password"><input type="password" value={current} onChange={(e) => setCurrent(e.target.value)} autoComplete="current-password" /></Field>
        <Field label="New password" hint="At least 8 characters."><input type="password" value={next} onChange={(e) => setNext(e.target.value)} autoComplete="new-password" /></Field>
        <Field label="Repeat new password"><input type="password" value={repeat} onChange={(e) => setRepeat(e.target.value)} autoComplete="new-password" /></Field>
      </div>
      <div className="row" style={{ marginTop: 12 }}>
        <button className="btn" disabled={saving || !current || next.length < 8}>{saving ? "Saving…" : "Change password"}</button>
        {msg && <span className={msg.ok ? "badge badge-ok" : "badge badge-bad"}>{msg.text}</span>}
      </div>
    </form>
  );
}
