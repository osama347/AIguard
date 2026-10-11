import { useEffect, useState } from "react";
import { ArrowsClockwise } from "@phosphor-icons/react";
import { notificationsEnabled, requestNotifications } from "../lib/notify";
import { api, assetUrl, checkForUpdate, isDesktop, serverUrl, setServerUrl, type CommunityInput } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { formatBytes } from "../lib/format";
import { cn } from "../lib/cn";
import { Badge, Button, Card, CardHead, ErrorBox, Fact, Facts, Field, FormSection, PageHeader, Spinner } from "../components/ui";

export function SettingsPage() {
  const { user, isAdmin, health, community, refreshCommunity, signOut, pushNotice } = useSession();
  const system = useAsync(() => api.system(), []);
  const [url, setUrl] = useState(serverUrl());
  const [form, setForm] = useState<CommunityInput>({});
  const [comErr, setComErr] = useState<string | null>(null);
  useEffect(() => {
    if (community) setForm({ name: community.name, address: community.address, city: community.city, country: community.country,
                             helpline: community.helpline, email: community.email, website: community.website });
  }, [community]);
  const setF = (k: keyof CommunityInput) => (e: React.ChangeEvent<HTMLInputElement>) => setForm({ ...form, [k]: e.target.value });
  const run = async (action: () => Promise<unknown>, ok: string) => {
    setComErr(null);
    try { await action(); await refreshCommunity(); pushNotice(ok); } catch (e) { setComErr((e as Error).message); }
  };

  // Logo is staged like every other form field: selecting a file changes nothing on
  // the server until "Save logo" is pressed, so there is always one clear submit.
  const [logo, setLogo] = useState<File | null>(null);
  const [logoPreview, setLogoPreview] = useState<string | null>(null);
  useEffect(() => {
    if (!logo) { setLogoPreview(null); return; }
    const url = URL.createObjectURL(logo);
    setLogoPreview(url);
    return () => URL.revokeObjectURL(url);
  }, [logo]);
  const [saveLogo, setSavingLogo] = useState(false);
  const saveLogoNow = async () => {
    if (!logo) return;
    setSavingLogo(true);
    setComErr(null);
    try {
      await api.uploadLogo(logo);
      await refreshCommunity();
      setLogo(null);
      pushNotice("Logo updated");
    } catch (e) {
      setComErr((e as Error).message);
    } finally {
      setSavingLogo(false);
    }
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
          <CardHead title="Community" sub="Shown on the login screen and in the sidebar." />
          <form className="flex flex-col gap-4" onSubmit={(e) => { e.preventDefault(); void run(() => api.saveCommunity(form), "Community details saved"); }}>
            <ErrorBox error={comErr} />
            <FormSection title="Details">
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
            </FormSection>
            <div>
              <Button variant="primary">Save community details</Button>
            </div>
          </form>
          <div className="mt-5 flex flex-wrap items-end gap-4 border-t border-line pt-5">
            <img
              className="logo-preview"
              src={logoPreview ?? (community?.logo_url ? assetUrl(community.logo_url) : undefined)}
              alt={community?.logo_url ? "Community logo" : "No logo"}
              style={!logoPreview && !community?.logo_url ? { borderStyle: "dashed" } : undefined}
            />
            <div className="flex min-w-[220px] flex-1 flex-col gap-2">
              <Field label="Logo" hint="PNG, JPEG or WebP, up to 2 MB. Selecting a file only stages it.">
                <input type="file" accept="image/png,image/jpeg,image/webp" onChange={(e) => setLogo(e.target.files?.[0] ?? null)} />
              </Field>
              <div className="flex flex-wrap items-center gap-2">
                <Button variant="primary" disabled={!logo || saveLogo} onClick={() => void saveLogoNow()}>
                  {saveLogo ? "Saving…" : "Save logo"}
                </Button>
                {community?.logo_url && (
                  <Button disabled={saveLogo} onClick={() => void run(() => api.deleteLogo(), "Logo removed")}>Remove logo</Button>
                )}
              </div>
              {logo && <span className="text-[12px] text-ok">New logo selected — press “Save logo” to apply it.</span>}
            </div>
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
  const { pushNotice } = useSession();
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
      setMsg(null);
      setCurrent(""); setNext(""); setRepeat("");
      pushNotice("Password changed");
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
          {msg && <span className={cn("text-[13px]", msg.ok ? "text-ok" : "text-bad")}>{msg.text}</span>}
        </div>
      </Card>
    </form>
  );
}
