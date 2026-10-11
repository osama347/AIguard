import { useEffect, useState } from "react";
import { ArrowsClockwise } from "@phosphor-icons/react";
import { notificationsEnabled, requestNotifications } from "../lib/notify";
import { api, assetUrl, checkForUpdate, isDesktop, serverUrl, setServerUrl, type CommunityInput } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { formatBytes } from "../lib/format";
import { Badge, Button, Card, CardHead, ErrorBox, Fact, Facts, Field, PageHeader, Spinner } from "../components/ui";

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
  const [updateMsg, setUpdateMsg] = useState<string | null>(null);
  const [checkingUpdate, setCheckingUpdate] = useState(false);
  const runUpdateCheck = async () => {
    setCheckingUpdate(true); setUpdateMsg(null);
    try {
      const r = await checkForUpdate();
      setUpdateMsg(r.available
        ? `Update ${r.latest_version} available — follow the install prompt.`
        : `You're up to date (${r.current_version}).`);
    } catch (e) {
      setUpdateMsg((e as Error).message);
    } finally {
      setCheckingUpdate(false);
    }
  };
  const models = system.data?.models as { state?: string; models?: string[]; trt_version?: string; device?: string; precision?: string } | null | undefined;

  return (
    <>
      <PageHeader title="Settings" />

      {isDesktop && (
        <Card>
          <CardHead title="Server" />
          <form className="flex flex-wrap items-end gap-3" onSubmit={(e) => { e.preventDefault(); setServerUrl(url || null); window.location.reload(); }}>
            <Field label="Guard++ server address" hint="The Jetson's address, for example http://192.168.1.20:8090">
              <input value={url} onChange={(e) => setUrl(e.target.value)} placeholder="http://192.168.1.20:8090" />
            </Field>
            <Button>Save &amp; reconnect</Button>
          </form>
          <div className="mt-4 flex flex-wrap items-center gap-3">
            <Button disabled={checkingUpdate} onClick={runUpdateCheck}>
              <ArrowsClockwise size={15} /> {checkingUpdate ? "Checking…" : "Check for updates"}
            </Button>
            {updateMsg && <span className="text-[12px] text-muted">{updateMsg}</span>}
          </div>
        </Card>
      )}

      {isAdmin && (
        <Card>
          <CardHead title="Community" />
          <form className="flex flex-col gap-4" onSubmit={(e) => { e.preventDefault(); void saveCommunity(() => api.saveCommunity(form), "Saved."); }}>
            <ErrorBox error={comErr} />
            {comMsg && <p className="text-[13px] text-muted">{comMsg}</p>}
            <Field label="Community name"><input value={form.name ?? ""} onChange={setF("name")} required maxLength={120} /></Field>
            <Field label="Address"><input value={form.address ?? ""} onChange={setF("address")} maxLength={300} /></Field>
            <div className="grid gap-4 sm:grid-cols-2">
              <Field label="City"><input value={form.city ?? ""} onChange={setF("city")} maxLength={100} /></Field>
              <Field label="Country"><input value={form.country ?? ""} onChange={setF("country")} maxLength={100} /></Field>
            </div>
            <div className="grid gap-4 sm:grid-cols-2">
              <Field label="Helpline"><input value={form.helpline ?? ""} onChange={setF("helpline")} maxLength={60} /></Field>
              <Field label="Email"><input type="email" value={form.email ?? ""} onChange={setF("email")} maxLength={120} /></Field>
            </div>
            <Field label="Website"><input value={form.website ?? ""} onChange={setF("website")} maxLength={200} /></Field>
            <div><Button variant="primary">Save community details</Button></div>
          </form>
          <div className="mt-5 flex flex-wrap items-center gap-4">
            {community?.logo_url && <img className="logo-preview" src={assetUrl(community.logo_url)} alt="Community logo" />}
            <Field label="Logo" hint="PNG, JPEG or WebP, up to 2 MB.">
              <input type="file" accept="image/png,image/jpeg,image/webp"
                     onChange={(e) => { const f = e.target.files?.[0]; if (f) void saveCommunity(() => api.uploadLogo(f), "Logo updated."); }} />
            </Field>
            {community?.logo_url && (
              <Button onClick={() => void saveCommunity(() => api.deleteLogo(), "Logo removed.")}>Remove logo</Button>
            )}
          </div>
        </Card>
      )}

      <Card>
        <CardHead title="Account" />
        <p className="m-0 text-[13px]">
          Signed in as <strong>{user?.full_name || user?.username}</strong>
          {user?.full_name && <span className="text-muted"> ({user.username})</span>}{" "}
          <Badge tone="info">{isAdmin ? "Administrator" : "Guard"}</Badge>
        </p>
        <div className="mt-4 flex flex-wrap gap-2">
          {!notif && (
            <Button onClick={() => requestNotifications().then(setNotif)}>Enable alert notifications</Button>
          )}
          <Button variant="danger" onClick={signOut}>Sign out</Button>
        </div>
      </Card>

      <PasswordCard />

      <Card>
        <CardHead title="System" />
        <ErrorBox error={system.error} onRetry={system.reload} />
        {!system.data ? <Spinner /> : (
          <Facts>
            <Fact label="Version">{system.data.version}</Fact>
            <Fact label="AI engine">
              <Badge tone={health?.inference.status === "ok" ? "ok" : "warn"}>{health?.inference.status ?? "unknown"}</Badge>
              {health?.inference.message && <span className="text-[12px] text-muted"> {health.inference.message}</span>}
            </Fact>
            {models?.device && <Fact label="Device">{models.device}</Fact>}
            {models?.trt_version && <Fact label="TensorRT">{models.trt_version} ({models.precision})</Fact>}
            {models?.models && <Fact label="Models">{models.models.join(", ")}</Fact>}
            <Fact label="Face match threshold">{system.data.policy.match_threshold.toFixed(2)}</Fact>
            <Fact label="Default analysis rate">{system.data.policy.sample_fps} frames/s</Fact>
            {system.data.live && <>
              <Fact label="Cameras">{system.data.live.cameras}</Fact>
              <Fact label="Visit ends after">{system.data.live.visit_gap_s} s with nothing in view</Fact>
              <Fact label="Repeat sightings">merged within {system.data.live.repeat_suppress_s} s</Fact>
              <Fact label="Snapshots kept">{system.data.live.snapshot_retention_days} days (the access log itself is kept)</Fact>
            </>}
            {health && <Fact label="Free disk">{formatBytes(health.disk.free_bytes)} of {formatBytes(health.disk.total_bytes)}</Fact>}
          </Facts>
        )}
      </Card>
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
    <form onSubmit={submit}>
      <Card>
        <CardHead title="Change password" />
        <div className="grid gap-4 sm:grid-cols-3">
          <Field label="Current password"><input type="password" value={current} onChange={(e) => setCurrent(e.target.value)} autoComplete="current-password" /></Field>
          <Field label="New password" hint="At least 8 characters."><input type="password" value={next} onChange={(e) => setNext(e.target.value)} autoComplete="new-password" /></Field>
          <Field label="Repeat new password"><input type="password" value={repeat} onChange={(e) => setRepeat(e.target.value)} autoComplete="new-password" /></Field>
        </div>
        <div className="mt-4 flex flex-wrap items-center gap-3">
          <Button disabled={saving || !current || next.length < 8}>{saving ? "Saving…" : "Change password"}</Button>
          {msg && <Badge tone={msg.ok ? "ok" : "bad"}>{msg.text}</Badge>}
        </div>
      </Card>
    </form>
  );
}
