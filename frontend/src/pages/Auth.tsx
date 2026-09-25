import { useEffect, useState, type FormEvent } from "react";
import { api, discoverServers, isDesktop, serverUrl, setServerUrl, setToken, type FoundServer } from "../api/client";
import { useSession } from "../session";
import { Brand, ErrorBox, Field, Spinner } from "../components/ui";

function AuthCard({ title, subtitle, wide, children }: { title: string; subtitle?: string; wide?: boolean; children: React.ReactNode }) {
  const { community } = useSession();
  return (
    <div className="auth-screen">
      <div className={`auth-card ${wide ? "setup-wide" : ""}`}>
        <div className="brand brand-large">
          <Brand community={community} size={34} />
        </div>
        {community?.configured && (community.helpline || community.address) && (
          <p className="muted small" style={{ textAlign: "center", margin: 0 }}>
            {[community.address, community.city].filter(Boolean).join(", ")}
            {community.helpline && <> · Helpline {community.helpline}</>}
          </p>
        )}
        <h1>{title}</h1>
        {subtitle && <p className="muted">{subtitle}</p>}
        {children}
      </div>
    </div>
  );
}

/** Shown when no Guard++ server is selected or reachable (desktop app: pick the Jetson on the LAN). */
export function ConnectPage() {
  const { healthError, refreshHealth } = useSession();
  const [url, setUrl] = useState(serverUrl() || (isDesktop ? "" : window.location.origin));
  const [busy, setBusy] = useState(false);
  const [found, setFound] = useState<FoundServer[] | null>(null);
  const [scanning, setScanning] = useState(false);

  const connect = async (target: string) => {
    setBusy(true);
    setServerUrl(target);
    await refreshHealth();
    setBusy(false);
  };
  const submit = (e: FormEvent) => { e.preventDefault(); void connect(url); };
  const scan = async () => {
    setScanning(true);
    try { setFound(await discoverServers()); } catch { setFound([]); } finally { setScanning(false); }
  };
  useEffect(() => { if (isDesktop) void scan(); }, []);

  const configured = !!serverUrl();
  return (
    <AuthCard
      title={isDesktop && !configured ? "Connect to Guard++" : "Cannot reach Guard++"}
      subtitle={isDesktop && !configured ? "Choose the Jetson that runs the Guard++ server on your network." : "The Guard++ service is not responding."}>
      {configured && <ErrorBox error={healthError} />}
      {isDesktop ? (
        <div className="stack">
          <div className="stack">
            <strong>Devices found on this network</strong>
            {scanning && <Spinner label="Searching…" />}
            {!scanning && found && found.length === 0 && <p className="muted">None found. Enter the address below.</p>}
            {found?.map((d) => (
              <button key={d.url} className="btn" disabled={busy} onClick={() => void connect(d.url)}>{d.name} — {d.url}</button>
            ))}
            {!scanning && <button type="button" className="btn btn-link" onClick={() => void scan()}>Search again</button>}
          </div>
          <form onSubmit={submit} className="stack">
            <Field label="Server address" hint="The Jetson's address and port, for example http://192.168.1.20:8090">
              <input value={url} onChange={(e) => setUrl(e.target.value)} placeholder="http://192.168.1.20:8090" required />
            </Field>
            <button className="btn btn-primary" disabled={busy}>{busy ? <Spinner /> : "Connect"}</button>
          </form>
        </div>
      ) : (
        <div className="stack">
          <p className="muted">Check that the <code>guard-core</code> service is running on the device.</p>
          <button className="btn btn-primary" onClick={() => refreshHealth()}>Try again</button>
        </div>
      )}
    </AuthCard>
  );
}

/** True when the server is on this same machine (then no setup code is needed). */
function serverIsLocal(): boolean {
  try {
    return /^(localhost|127\.0\.0\.1|\[::1\])$/.test(new URL(serverUrl() || window.location.origin).hostname);
  } catch { return false; }
}

export function SetupPage() {
  const { signIn, refreshHealth, refreshCommunity } = useSession();
  const needsCode = !serverIsLocal();
  const [username, setUsername] = useState("admin");
  const [password, setPassword] = useState("");
  const [confirm, setConfirm] = useState("");
  const [code, setCode] = useState("");
  const [c, setC] = useState({ name: "", address: "", city: "", country: "", helpline: "", email: "", website: "" });
  const [logo, setLogo] = useState<File | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const set = (k: keyof typeof c) => (e: React.ChangeEvent<HTMLInputElement>) => setC({ ...c, [k]: e.target.value });

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    if (password !== confirm) return setError("Passwords do not match.");
    setBusy(true);
    setError(null);
    try {
      const r = await api.setup(username, password, { setup_code: needsCode ? code.trim() : undefined, community: c });
      setToken(r.token);                       // the logo upload below needs the new admin's session
      if (logo) {
        try { await api.uploadLogo(logo); } catch (err) { setError(`Account created, but the logo was not saved: ${(err as Error).message}`); }
      }
      await refreshCommunity();
      signIn(r.token, r.user);
      await refreshHealth();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthCard wide title="Welcome to Guard++" subtitle="Set up this Guard++ server: your community and the administrator account.">
      <form onSubmit={submit} className="stack">
        <ErrorBox error={error} />
        <div className="form-section">Your community</div>
        <Field label="Community name" hint="For example: Bahria Town, or Pak-Austria Fachhochschule">
          <input value={c.name} onChange={set("name")} required maxLength={120} />
        </Field>
        <Field label="Address"><input value={c.address} onChange={set("address")} maxLength={300} /></Field>
        <div className="row">
          <Field label="City"><input value={c.city} onChange={set("city")} maxLength={100} /></Field>
          <Field label="Country"><input value={c.country} onChange={set("country")} maxLength={100} /></Field>
        </div>
        <div className="row">
          <Field label="Helpline"><input value={c.helpline} onChange={set("helpline")} inputMode="tel" maxLength={60} /></Field>
          <Field label="Email"><input type="email" value={c.email} onChange={set("email")} maxLength={120} /></Field>
        </div>
        <Field label="Website"><input value={c.website} onChange={set("website")} maxLength={200} placeholder="https://" /></Field>
        <Field label="Logo (optional)" hint="PNG, JPEG or WebP, up to 2 MB.">
          <input type="file" accept="image/png,image/jpeg,image/webp" onChange={(e) => setLogo(e.target.files?.[0] ?? null)} />
        </Field>
        {logo && <img className="logo-preview" src={URL.createObjectURL(logo)} alt="Logo preview" />}

        <div className="form-section">Administrator account</div>
        {needsCode && (
          <Field label="Setup code" hint="Shown when the server was installed. On the Jetson: sudo cat /etc/guard/guard.env">
            <input value={code} onChange={(e) => setCode(e.target.value)} autoComplete="off" required />
          </Field>
        )}
        <Field label="Username">
          <input value={username} onChange={(e) => setUsername(e.target.value)} autoComplete="username" required minLength={3} />
        </Field>
        <Field label="Password" hint="At least 8 characters.">
          <input type="password" value={password} onChange={(e) => setPassword(e.target.value)} autoComplete="new-password" required minLength={8} />
        </Field>
        <Field label="Confirm password">
          <input type="password" value={confirm} onChange={(e) => setConfirm(e.target.value)} autoComplete="new-password" required />
        </Field>
        <button className="btn btn-primary" disabled={busy}>{busy ? <Spinner /> : "Finish setup"}</button>
      </form>
    </AuthCard>
  );
}

export function LoginPage() {
  const { signIn } = useSession();
  const [username, setUsername] = useState("");
  const [password, setPassword] = useState("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      const r = await api.login(username, password);
      signIn(r.token, r.user);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <AuthCard title="Sign in">
      <form onSubmit={submit} className="stack">
        <ErrorBox error={error} />
        <Field label="Username">
          <input value={username} onChange={(e) => setUsername(e.target.value)} autoComplete="username" autoFocus required />
        </Field>
        <Field label="Password">
          <input type="password" value={password} onChange={(e) => setPassword(e.target.value)} autoComplete="current-password" required />
        </Field>
        <button className="btn btn-primary" disabled={busy}>{busy ? <Spinner /> : "Sign in"}</button>
        {isDesktop && (
          <button type="button" className="btn btn-link" onClick={() => { setServerUrl(null); window.location.reload(); }}>
            Server: {serverUrl()} — reset
          </button>
        )}
      </form>
    </AuthCard>
  );
}
