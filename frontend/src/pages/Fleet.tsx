import { useEffect, useMemo, useState, type FormEvent } from "react";
import { useSearchParams } from "react-router-dom";
import { api, fetchProtectedImage, type Driver, type EnrollResult, type Status, type Vehicle } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { STATUS_TONE, copyToClipboard, isValidPhone, whatsAppShareUrl } from "../lib/format";
import { cn } from "../lib/cn";
import { Badge, Button, Card, Empty, ErrorBox, Field, Modal, Notice, PageHeader, Select, Spinner } from "../components/ui";

const STATUSES: Status[] = ["active", "inactive", "blacklisted"];
const PHOTO_ERRORS: Record<string, string> = { no_face: "no face found", invalid_image: "not a valid image" };

const LINK = "text-accent hover:underline";
const HEADING = "text-[11px] font-semibold uppercase tracking-wider text-faint";

function useFilter<T>(items: T[] | undefined, text: (t: T) => string) {
  const [q, setQ] = useState("");
  const filtered = useMemo(
    () => (items ?? []).filter((i) => text(i).toLowerCase().includes(q.trim().toLowerCase())),
    [items, q, text],
  );
  return { q, setQ, filtered };
}

function StatusSelect({ value, onChange }: { value: Status; onChange: (s: Status) => void }) {
  return <Select value={value} onValueChange={(v) => onChange(v as Status)} options={STATUSES.map((s) => ({ value: s, label: s }))} className="w-full" />;
}

function EnrollSummary({ result }: { result: EnrollResult | null }) {
  if (!result) return null;
  const failed = result.photos.filter((p) => !p.enrolled);
  return (
    <Notice tone={failed.length ? "warn" : "ok"}>
      {result.enrolled} photo(s) enrolled.
      {failed.map((p) => <div key={p.filename} className="text-[12px]">{p.filename}: {PHOTO_ERRORS[p.error ?? ""] ?? p.error}</div>)}
    </Notice>
  );
}

/** Renders Driver.photo_url, which requires admin auth that a plain <img src> can't
 *  send — fetched as a blob instead. `size` picks the CSS class (small table thumbnail
 *  vs. the larger editor preview); with no photo, renders the same-sized empty circle. */
function DriverAvatar({ photoUrl, size }: { photoUrl?: string | null; size: "sm" | "lg" }) {
  const [blobUrl, setBlobUrl] = useState<string | null>(null);
  const cls = size === "lg" ? "avatar-lg" : "avatar";

  useEffect(() => {
    if (!photoUrl) { setBlobUrl(null); return; }
    let objectUrl: string | null = null;
    let cancelled = false;
    fetchProtectedImage(photoUrl).then((blob) => {
      if (cancelled) return;
      objectUrl = URL.createObjectURL(blob);
      setBlobUrl(objectUrl);
    }).catch(() => { if (!cancelled) setBlobUrl(null); });
    return () => { cancelled = true; if (objectUrl) URL.revokeObjectURL(objectUrl); };
  }, [photoUrl]);

  return blobUrl ? <img className={cls} src={blobUrl} alt="" /> : <span className={cls} />;
}

// ====================================================================== drivers ("People" tab)

/** The "People" tab of the Fleet page: owners and the drivers they've authorized.
 *  Creation always happens from the Vehicles tab (registering a vehicle enrolls its
 *  owner; redeeming a code adds another driver) — this tab is the roster + editor. */
function PeopleTab({ drivers, vehicles, isAdmin, onChanged }: {
  drivers: ReturnType<typeof useAsync<Driver[]>>; vehicles: Vehicle[]; isAdmin: boolean; onChanged: () => void;
}) {
  const [editing, setEditing] = useState<Driver | null>(null);
  const textOf = useMemo(() => (d: Driver) => `${d.name} ${d.phone} ${d.vehicles.map((v) => v.plate_number).join(" ")}`, []);
  const { q, setQ, filtered } = useFilter(drivers.data, textOf);

  return (
    <>
      <ErrorBox error={drivers.error} onRetry={drivers.reload} />
      {drivers.loading && !drivers.data ? <div className="grid place-items-center py-16"><Spinner /></div> : !drivers.data?.length ? (
        <Empty title="No people yet">Register a vehicle to add its owner.</Empty>
      ) : (
        <Card flush>
          <div className="border-b border-line p-3"><input className="max-w-[320px]" placeholder="Search people…" value={q} onChange={(e) => setQ(e.target.value)} /></div>
          <div className="overflow-x-auto">
            <table>
              <thead><tr>{isAdmin && <th></th>}<th>Name</th><th>Status</th><th>Face photos</th><th>Vehicles</th><th>Phone</th></tr></thead>
              <tbody>
                {filtered.map((d) => (
                  <tr key={d.id} className="cursor-pointer" onClick={() => setEditing(d)}>
                    {isAdmin && <td><DriverAvatar photoUrl={d.photo_url} size="sm" /></td>}
                    <td><strong className="font-medium">{d.name}</strong>{d.is_owner && <span className="text-[12px] text-muted"> · Owner</span>}</td>
                    <td><Badge tone={STATUS_TONE[d.status]}>{d.status}</Badge></td>
                    <td>{d.photo_count ? d.photo_count : <Badge tone="warn">none</Badge>}</td>
                    <td className="font-mono">{d.vehicles.map((v) => v.plate_number).join(", ") || "—"}</td>
                    <td>{d.phone || "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Card>
      )}
      {editing && (
        <DriverEditor
          driver={editing}
          vehicles={vehicles}
          readOnly={!isAdmin}
          onClose={() => setEditing(null)}
          onChanged={(d) => { if (d) setEditing(d); onChanged(); }}
        />
      )}
    </>
  );
}

function DriverEditor({ driver, vehicles, readOnly, onClose, onChanged }: {
  driver: Driver; vehicles: Vehicle[]; readOnly: boolean; onClose: () => void; onChanged: (d?: Driver) => void;
}) {
  const [form, setForm] = useState({ name: driver.name, phone: driver.phone, notes: driver.notes, status: driver.status });
  const [photos, setPhotos] = useState<File[]>([]);
  const [enrolled, setEnrolled] = useState<EnrollResult | null>(null);
  const [assignTo, setAssignTo] = useState<number | "">("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const run = async (fn: () => Promise<Driver | void>) => {
    setBusy(true);
    setError(null);
    try {
      const d = await fn();
      onChanged(d || undefined);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const unassigned = vehicles.filter((v) => !driver.vehicles.some((dv) => dv.id === v.id));

  return (
    <Modal title={driver.name} onClose={onClose} wide>
      <ErrorBox error={error} />
      <div className="grid gap-6 md:grid-cols-2">
        <form className="flex flex-col gap-3" onSubmit={(e) => { e.preventDefault(); run(() => api.updateDriver(driver.id, form)); }}>
          <h3 className={HEADING}>Details</h3>
          {!readOnly && (
            <div className="flex items-center gap-3">
              <DriverAvatar photoUrl={driver.photo_url} size="lg" />
              <div className="flex flex-col gap-1">
                <input type="file" accept="image/png,image/jpeg,image/webp" disabled={busy}
                       onChange={(e) => { const f = e.target.files?.[0]; if (f) run(() => api.setDriverPhoto(driver.id, f)); }} />
                <span className="text-[12px] text-muted">PNG, JPEG or WebP, up to 2 MB.</span>
                {driver.photo_url && (
                  <button type="button" className={cn("self-start text-[13px]", LINK)} disabled={busy}
                    onClick={() => run(() => api.deleteDriverPhoto(driver.id))}>Remove photo</button>
                )}
              </div>
            </div>
          )}
          <Field label="Full name"><input value={form.name} disabled={readOnly} onChange={(e) => setForm({ ...form, name: e.target.value })} required /></Field>
          <Field label="Status" hint="Blacklisted drivers are always denied and raise a critical alert.">
            <StatusSelect value={form.status} onChange={(status) => setForm({ ...form, status })} />
          </Field>
          <Field label="Phone"><input value={form.phone} disabled={readOnly} onChange={(e) => setForm({ ...form, phone: e.target.value })} /></Field>
          <Field label="Notes"><textarea rows={3} value={form.notes} disabled={readOnly} onChange={(e) => setForm({ ...form, notes: e.target.value })} /></Field>
          {!readOnly && <div className="flex flex-wrap gap-2">
            <Button variant="primary" disabled={busy}>Save</Button>
            <Button type="button" variant="danger" disabled={busy}
              onClick={() => confirm(`Delete ${driver.name}?`) && run(async () => { await api.deleteDriver(driver.id); onClose(); })}>Delete</Button>
          </div>}
        </form>

        <div className="flex flex-col gap-3">
          <h3 className={HEADING}>Face photos <span className="font-normal normal-case tracking-normal text-muted">({driver.photo_count} enrolled)</span></h3>
          {!readOnly && (
            <>
              <input type="file" accept="image/*" multiple onChange={(e) => setPhotos(Array.from(e.target.files ?? []))} />
              <div className="flex flex-wrap gap-2">
                <Button disabled={busy || !photos.length}
                  onClick={() => run(async () => { const r = await api.enrollPhotos(driver.id, photos); setEnrolled(r); setPhotos([]); return r.driver; })}>
                  Enroll {photos.length || ""} photo(s)
                </Button>
                {driver.photo_count > 0 && (
                  <button type="button" className={cn("text-[13px]", LINK)} disabled={busy}
                    onClick={() => confirm("Remove all enrolled face photos?") && run(async () => { await api.clearPhotos(driver.id); return { ...driver, photo_count: 0 }; })}>
                    Remove all
                  </button>
                )}
              </div>
              <EnrollSummary result={enrolled} />
            </>
          )}

          <h3 className={HEADING}>Authorized vehicles</h3>
          {driver.vehicles.length === 0 && <p className="text-[13px] text-muted">Not authorized for any vehicle.</p>}
          <ul className="m-0 list-none p-0">
            {driver.vehicles.map((v) => (
              <li key={v.id} className="flex items-center justify-between gap-3 border-b border-line py-2 last:border-b-0">
                <span className="font-mono">{v.plate_number}</span>
                {!readOnly && <Button size="sm" disabled={busy}
                  onClick={() => run(async () => { await api.unassign(driver.id, v.id); return { ...driver, vehicles: driver.vehicles.filter((x) => x.id !== v.id) }; })}>
                  Remove</Button>}
              </li>
            ))}
          </ul>
          {!readOnly && unassigned.length > 0 && (
            <div className="flex flex-wrap items-center gap-2">
              <Select value={assignTo === "" ? "__none__" : String(assignTo)} onValueChange={(v) => setAssignTo(v === "__none__" ? "" : Number(v))}
                      className="flex-1" placeholder="Choose vehicle…"
                      options={[{ value: "__none__", label: "Choose vehicle…" }, ...unassigned.map((v) => ({ value: String(v.id), label: `${v.plate_number}${v.make ? ` · ${v.make} ${v.model}` : ""}` }))]} />
              <Button disabled={busy || assignTo === ""}
                onClick={() => run(async () => { const d = await api.assign(driver.id, Number(assignTo)); setAssignTo(""); return d; })}>Authorize</Button>
            </div>
          )}
        </div>
      </div>
    </Modal>
  );
}

/** Redeems an owner's authorization code to add a new authorized driver to their vehicle.
 *  Reused standalone (code typed in, e.g. from the Vehicles page) and inside the
 *  registration wizard, where the code was just generated and is pre-filled. */
function DriverAuthorizeForm({ code: initialCode, codeReadOnly, onDone }: {
  code?: string; codeReadOnly?: boolean; onDone: (d: Driver) => void;
}) {
  const [code, setCode] = useState(initialCode ?? "");
  const [name, setName] = useState("");
  const [phone, setPhone] = useState("");
  const [photos, setPhotos] = useState<File[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      let d = await api.authorizeDriver(code.trim(), { name, phone });
      if (photos.length) d = (await api.enrollPhotos(d.id, photos)).driver;
      setName(""); setPhone(""); setPhotos([]);
      onDone(d);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <form onSubmit={submit} className="flex flex-col gap-3">
      <ErrorBox error={error} />
      <Field label="Authorization code" hint="From the vehicle owner — shown on their vehicle's profile.">
        <input value={code} onChange={(e) => setCode(e.target.value)} required disabled={codeReadOnly}
               autoFocus={!codeReadOnly} className="font-mono" placeholder="XXXX-XXXX" />
      </Field>
      <Field label="Full name"><input value={name} onChange={(e) => setName(e.target.value)} required autoFocus={codeReadOnly} maxLength={120} /></Field>
      <Field label="Phone (optional)"><input value={phone} onChange={(e) => setPhone(e.target.value)} maxLength={40} /></Field>
      <Field label="Face photos" hint="Front-facing, well lit, one person per photo. You can add more later.">
        <input type="file" accept="image/*" multiple onChange={(e) => setPhotos(Array.from(e.target.files ?? []))} />
      </Field>
      <Button variant="primary" disabled={busy}>{busy ? <Spinner /> : "Add authorized driver"}</Button>
    </form>
  );
}

// ====================================================================== vehicles

/** Comma-separated driver names, each clickable to open their profile card without
 *  triggering whatever click handler the surrounding row/text has (e.g. opening a vehicle editor). */
function DriverNames({ drivers, onPick }: { drivers: { id: number; name: string }[]; onPick: (id: number) => void }) {
  if (!drivers.length) return <>—</>;
  return (
    <>
      {drivers.map((d, i) => (
        <span key={d.id}>
          {i > 0 && ", "}
          <button type="button" className={LINK} onClick={(e) => { e.stopPropagation(); onPick(d.id); }}>{d.name}</button>
        </span>
      ))}
    </>
  );
}

/** Read-only profile card for a driver, opened by clicking their name from the Vehicles page. */
function DriverCard({ driverId, onClose }: { driverId: number; onClose: () => void }) {
  const driver = useAsync(() => api.driver(driverId), [driverId]);
  return (
    <Modal title="Driver" onClose={onClose}>
      <ErrorBox error={driver.error} onRetry={driver.reload} />
      {!driver.data ? <Spinner /> : (
        <div className="flex flex-col gap-3">
          <div className="flex items-center justify-between gap-3">
            <h2 className="text-[15px] font-semibold">{driver.data.name}</h2>
            <Badge tone={STATUS_TONE[driver.data.status]}>{driver.data.status}</Badge>
          </div>
          <dl className="m-0 grid grid-cols-[max-content_minmax(0,1fr)] items-baseline gap-x-5 gap-y-2">
            <dt className="text-muted">Phone</dt><dd className="m-0">{driver.data.phone || "—"}</dd>
            <dt className="text-muted">Face photos</dt><dd className="m-0">{driver.data.photo_count ? `${driver.data.photo_count} enrolled` : <Badge tone="warn">none</Badge>}</dd>
            <dt className="text-muted">Vehicles</dt><dd className="m-0 font-mono">{driver.data.vehicles.map((v) => v.plate_number).join(", ") || "—"}</dd>
            {driver.data.notes && <><dt className="text-muted">Notes</dt><dd className="m-0">{driver.data.notes}</dd></>}
          </dl>
        </div>
      )}
    </Modal>
  );
}

/** Copies a code with a brief "Copied!" confirmation; works on plain HTTP (see copyToClipboard). */
function CopyCodeButton({ value }: { value: string }) {
  const [copied, setCopied] = useState(false);
  return (
    <Button onClick={async () => {
      if (await copyToClipboard(value)) { setCopied(true); setTimeout(() => setCopied(false), 1500); }
    }}>
      {copied ? "Copied!" : "Copy"}
    </Button>
  );
}

/** Name + mandatory phone/WhatsApp + notes: the owner-specific fields, reused by the
 *  registration wizard (step 2) and by backfilling a legacy vehicle's owner. Doesn't
 *  call the API itself — the caller decides whether that's a combined vehicle+owner
 *  create or a plain "set owner" call. */
function OwnerStep({ onBack, onNext, submitLabel }: {
  onBack?: () => void; onNext: (owner: { name: string; phone: string; notes: string }) => void; submitLabel: string;
}) {
  const [name, setName] = useState("");
  const [phone, setPhone] = useState("");
  const [notes, setNotes] = useState("");
  const [touched, setTouched] = useState(false);
  const phoneOk = isValidPhone(phone);

  const submit = (e: FormEvent) => {
    e.preventDefault();
    setTouched(true);
    if (!name.trim() || !phoneOk) return;
    onNext({ name: name.trim(), phone: phone.trim(), notes });
  };

  return (
    <form onSubmit={submit} className="flex flex-col gap-3">
      <Field label="Owner's full name"><input value={name} onChange={(e) => setName(e.target.value)} required autoFocus maxLength={120} /></Field>
      <Field label="Mobile / WhatsApp number" hint="Required — this is how the authorization code reaches them.">
        <input value={phone} onChange={(e) => setPhone(e.target.value)} required placeholder="+92 300 1234567" />
      </Field>
      {touched && !phoneOk && <ErrorBox error="Enter a valid phone number (7-15 digits)." />}
      <Field label="Notes (optional)"><textarea rows={2} value={notes} onChange={(e) => setNotes(e.target.value)} /></Field>
      <div className="flex items-center gap-2">
        {onBack && <Button type="button" onClick={onBack}>Back</Button>}
        <span className="flex-1" />
        <Button variant="primary">{submitLabel}</Button>
      </div>
    </form>
  );
}

/** Backfills an owner onto a vehicle registered before this feature existed. */
function SetOwnerModal({ vehicle, onClose, onSaved }: { vehicle: Vehicle; onClose: () => void; onSaved: () => void }) {
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const submit = async (owner: { name: string; phone: string; notes: string }) => {
    setBusy(true);
    setError(null);
    try {
      await api.setVehicleOwner(vehicle.id, owner);
      onSaved();
    } catch (err) {
      setError((err as Error).message);
      setBusy(false);
    }
  };
  return (
    <Modal title={`Set owner for ${vehicle.plate_number}`} onClose={onClose}>
      <ErrorBox error={error} />
      {busy ? <Spinner /> : <OwnerStep onNext={submit} submitLabel="Set owner" />}
    </Modal>
  );
}

type WizardStep = "vehicle" | "owner" | "drivers";

/** Register a vehicle → its owner → (optionally) further authorized drivers, in that
 *  order. Vehicle + owner are submitted together as one atomic request on the owner
 *  step; only the vehicle fields live in local state until then. */
export function RegisterVehicleWizard({ initialPlate, onClose, onDone }: {
  initialPlate?: string; onClose: () => void; onDone: () => void;
}) {
  const [step, setStep] = useState<WizardStep>("vehicle");
  const [vehicleForm, setVehicleForm] = useState({ plate_number: initialPlate ?? "", make: "", model: "", color: "" });
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [created, setCreated] = useState<Vehicle | null>(null);
  const [addedDrivers, setAddedDrivers] = useState<Driver[]>([]);
  const setV = (k: keyof typeof vehicleForm) => (e: { target: { value: string } }) => setVehicleForm({ ...vehicleForm, [k]: e.target.value });

  const submitVehicleStep = (e: FormEvent) => {
    e.preventDefault();
    if (!vehicleForm.plate_number.trim()) return;
    setStep("owner");
  };

  const submitOwner = async (owner: { name: string; phone: string; notes: string }) => {
    setBusy(true);
    setError(null);
    try {
      const vehicle = await api.createVehicle({ ...vehicleForm, owner });
      setCreated(vehicle);
      setStep("drivers");
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <Modal title="Register a vehicle" onClose={onClose} wide>
      <ErrorBox error={error} />
      {step === "vehicle" && (
        <form onSubmit={submitVehicleStep} className="flex flex-col gap-3">
          <p className="text-[12px] text-muted">Step 1 of 3 — vehicle</p>
          <Field label="Plate number"><input value={vehicleForm.plate_number} onChange={setV("plate_number")} required autoFocus className="font-mono" /></Field>
          <div className="grid gap-3 md:grid-cols-3">
            <Field label="Make"><input value={vehicleForm.make} onChange={setV("make")} /></Field>
            <Field label="Model"><input value={vehicleForm.model} onChange={setV("model")} /></Field>
            <Field label="Color"><input value={vehicleForm.color} onChange={setV("color")} /></Field>
          </div>
          <div className="flex items-center"><span className="flex-1" /><Button variant="primary">Next: Owner</Button></div>
        </form>
      )}

      {step === "owner" && (
        <div className="flex flex-col gap-3">
          <p className="text-[12px] text-muted">Step 2 of 3 — owner (auto-enrolled and authorized to drive this vehicle)</p>
          {busy ? <Spinner /> : <OwnerStep onBack={() => setStep("vehicle")} onNext={submitOwner} submitLabel="Register vehicle" />}
        </div>
      )}

      {step === "drivers" && created && (
        <div className="flex flex-col gap-3">
          <p className="text-[12px] text-muted">Step 3 of 3 — authorized drivers (optional)</p>
          <Notice tone="ok">
            <strong>{created.plate_number} registered.</strong> Owner: {created.owner?.name}.
          </Notice>

          {created.auth_code && (
            <Field label="Authorization code" hint="Share this with anyone the owner authorizes to drive — an admin can add them later with it, without the owner present.">
              <div className="flex flex-wrap items-center gap-2">
                <input value={created.auth_code} readOnly className="flex-1 font-mono" />
                <CopyCodeButton value={created.auth_code} />
                {created.owner?.phone && (
                  <a className="inline-flex h-9 items-center rounded-md border border-line-strong bg-surface px-3.5 text-sm font-medium hover:bg-surface-2 hover:no-underline" target="_blank" rel="noreferrer"
                     href={whatsAppShareUrl(created.owner.phone, `Guard++ authorization code for ${created.plate_number}: ${created.auth_code}`)}>
                    Share via WhatsApp
                  </a>
                )}
              </div>
            </Field>
          )}

          {addedDrivers.length > 0 && (
            <>
              <h3 className={HEADING}>Added so far</h3>
              <ul className="m-0 list-none p-0">
                {addedDrivers.map((d) => <li key={d.id} className="border-b border-line py-2 last:border-b-0">{d.name}</li>)}
              </ul>
            </>
          )}
          <DriverAuthorizeForm code={created.auth_code ?? ""} codeReadOnly onDone={(d) => setAddedDrivers((a) => [...a, d])} />

          <div className="flex items-center"><span className="flex-1" /><Button variant="primary" onClick={onDone}>Done</Button></div>
        </div>
      )}
    </Modal>
  );
}

/** The "Vehicles" tab of the Fleet page. */
function VehiclesTab({ vehicles, isAdmin, onChanged }: {
  vehicles: ReturnType<typeof useAsync<Vehicle[]>>; isAdmin: boolean; onChanged: () => void;
}) {
  const [editing, setEditing] = useState<Vehicle | null>(null);
  const [settingOwnerFor, setSettingOwnerFor] = useState<Vehicle | null>(null);
  const [viewingDriver, setViewingDriver] = useState<number | null>(null);
  const textOf = useMemo(() => (v: Vehicle) =>
    `${v.plate_number} ${v.plate_normalized} ${v.make} ${v.model} ${v.color} ${v.owner?.name ?? ""}`, []);
  const { q, setQ, filtered } = useFilter(vehicles.data, textOf);

  return (
    <>
      <ErrorBox error={vehicles.error} onRetry={vehicles.reload} />
      {vehicles.loading && !vehicles.data ? <div className="grid place-items-center py-16"><Spinner /></div> : !vehicles.data?.length ? (
        <Empty title="No vehicles yet">Register a vehicle and its owner to get started.</Empty>
      ) : (
        <Card flush>
          <div className="border-b border-line p-3"><input className="max-w-[320px]" placeholder="Search plates or owners…" value={q} onChange={(e) => setQ(e.target.value)} /></div>
          <div className="overflow-x-auto">
            <table>
              <thead><tr><th>Plate</th><th>Status</th><th>Vehicle</th><th>Owner</th><th>Other authorized drivers</th></tr></thead>
              <tbody>
                {filtered.map((v) => (
                  <tr key={v.id} className="cursor-pointer" onClick={() => setEditing(v)}>
                    <td className="font-mono"><strong className="font-medium">{v.plate_number}</strong></td>
                    <td><Badge tone={STATUS_TONE[v.status]}>{v.status}</Badge></td>
                    <td>{[v.color, v.make, v.model].filter(Boolean).join(" ") || "—"}</td>
                    <td>
                      {v.owner ? (
                        <button type="button" className={LINK} onClick={(e) => { e.stopPropagation(); setViewingDriver(v.owner!.id); }}>
                          {v.owner.name}
                        </button>
                      ) : (
                        <button type="button" className="hover:no-underline" onClick={(e) => { e.stopPropagation(); setSettingOwnerFor(v); }}>
                          <Badge tone="warn">No owner set</Badge>
                        </button>
                      )}
                    </td>
                    <td><DriverNames drivers={v.drivers.filter((d) => d.id !== v.owner?.id)} onPick={setViewingDriver} /></td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Card>
      )}
      {editing && (
        <VehicleEditor vehicle={editing} readOnly={!isAdmin}
          onClose={() => setEditing(null)} onSaved={() => { setEditing(null); onChanged(); }}
          onPickDriver={setViewingDriver} />
      )}
      {settingOwnerFor && (
        <SetOwnerModal vehicle={settingOwnerFor} onClose={() => setSettingOwnerFor(null)}
          onSaved={() => { setSettingOwnerFor(null); onChanged(); }} />
      )}
      {viewingDriver !== null && <DriverCard driverId={viewingDriver} onClose={() => setViewingDriver(null)} />}
    </>
  );
}

type FleetTab = "vehicles" | "people";

/** Vehicles and the people authorized to drive them, as one page: both kinds of
 *  record are created together (registering a vehicle enrolls its owner; redeeming
 *  a code adds another driver), so a single page with tabs beats two separate ones. */
export function FleetPage() {
  const { user } = useSession();
  const isAdmin = user?.role === "admin";
  const [params, setParams] = useSearchParams();
  const tab: FleetTab = params.get("tab") === "people" ? "people" : "vehicles";
  const setTab = (t: FleetTab) => setParams(t === "vehicles" ? {} : { tab: t }, { replace: true });

  const vehicles = useAsync(() => api.vehicles(), []);
  const drivers = useAsync(() => api.drivers(), []);
  const [registering, setRegistering] = useState(false);
  const [authorizing, setAuthorizing] = useState(false);
  const refreshAll = () => { vehicles.reload(); drivers.reload(); };

  const tabBtn = (on: boolean) =>
    cn(
      "-mb-px border-b-2 px-3 py-2 text-[13.5px] font-medium transition-colors",
      on ? "border-text text-text" : "border-transparent text-muted hover:text-text",
    );

  return (
    <>
      <PageHeader
        title="Fleet"
        subtitle="Vehicles and the people authorized to drive them. Plates are matched ignoring spaces, dashes and case."
        actions={isAdmin && (
          <div className="flex flex-wrap gap-2">
            <Button onClick={() => setAuthorizing(true)}>Add driver by code</Button>
            <Button variant="primary" onClick={() => setRegistering(true)}>Register vehicle</Button>
          </div>
        )}
      />
      <div className="mb-4 flex gap-1 border-b border-line">
        <button type="button" className={tabBtn(tab === "vehicles")} onClick={() => setTab("vehicles")}>
          Vehicles{vehicles.data ? ` (${vehicles.data.length})` : ""}
        </button>
        <button type="button" className={tabBtn(tab === "people")} onClick={() => setTab("people")}>
          People{drivers.data ? ` (${drivers.data.length})` : ""}
        </button>
      </div>

      {tab === "vehicles"
        ? <VehiclesTab vehicles={vehicles} isAdmin={isAdmin} onChanged={refreshAll} />
        : <PeopleTab drivers={drivers} vehicles={vehicles.data ?? []} isAdmin={isAdmin} onChanged={refreshAll} />}

      {registering && <RegisterVehicleWizard onClose={() => setRegistering(false)} onDone={() => { setRegistering(false); refreshAll(); }} />}
      {authorizing && (
        <Modal title="Add driver by code" onClose={() => setAuthorizing(false)}>
          <DriverAuthorizeForm onDone={() => { setAuthorizing(false); refreshAll(); }} />
        </Modal>
      )}
    </>
  );
}

function VehicleEditor({ vehicle, readOnly, onClose, onSaved, onPickDriver }: {
  vehicle: Vehicle; readOnly: boolean; onClose: () => void; onSaved: () => void; onPickDriver: (id: number) => void;
}) {
  const [form, setForm] = useState({
    plate_number: vehicle.plate_number, make: vehicle.make, model: vehicle.model,
    color: vehicle.color, status: vehicle.status,
  });
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [settingOwner, setSettingOwner] = useState(false);
  const set = (k: keyof typeof form) => (e: { target: { value: string } }) => setForm({ ...form, [k]: e.target.value });

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await api.updateVehicle(vehicle.id, form);
      onSaved();
    } catch (err) {
      setError((err as Error).message);
      setBusy(false);
    }
  };

  const remove = async () => {
    if (!confirm(`Delete vehicle ${vehicle.plate_number}?`)) return;
    try { await api.deleteVehicle(vehicle.id); onSaved(); } catch (err) { setError((err as Error).message); }
  };

  const setOwner = async (owner: { name: string; phone: string; notes: string }) => {
    try { await api.setVehicleOwner(vehicle.id, owner); onSaved(); } catch (err) { setError((err as Error).message); }
  };

  const otherDrivers = vehicle.drivers.filter((d) => d.id !== vehicle.owner?.id);

  return (
    <Modal title={vehicle.plate_number} onClose={onClose} wide
      footer={!readOnly && <>
        <Button variant="danger" onClick={remove} disabled={busy}>Delete</Button>
        <span className="flex-1" />
        <Button onClick={onClose}>Cancel</Button>
        <Button variant="primary" form="vehicle-form" disabled={busy}>{busy ? <Spinner /> : "Save"}</Button>
      </>}>
      <div className="flex flex-col gap-3">
        <form id="vehicle-form" onSubmit={submit} className="flex flex-col gap-3">
          <ErrorBox error={error} />
          <Field label="Plate number"><input value={form.plate_number} onChange={set("plate_number")} required disabled={readOnly} autoFocus className="font-mono" /></Field>
          <div className="grid gap-3 md:grid-cols-3">
            <Field label="Make"><input value={form.make} onChange={set("make")} disabled={readOnly} /></Field>
            <Field label="Model"><input value={form.model} onChange={set("model")} disabled={readOnly} /></Field>
            <Field label="Color"><input value={form.color} onChange={set("color")} disabled={readOnly} /></Field>
          </div>
          <Field label="Status" hint="Blacklisted vehicles are always denied.">
            <StatusSelect value={form.status} onChange={(status) => setForm({ ...form, status })} />
          </Field>
        </form>

        <h3 className={HEADING}>Owner</h3>
        {vehicle.owner ? (
          <p className="m-0 text-[13px]">
            <button type="button" className={LINK} onClick={() => onPickDriver(vehicle.owner!.id)}>{vehicle.owner.name}</button>
            {" — "}{vehicle.owner.phone}
          </p>
        ) : (
          <div className="flex flex-col gap-3">
            <p className="m-0 text-[13px] text-muted">No owner set — this vehicle was registered before this feature existed.</p>
            {!readOnly && !settingOwner && <Button onClick={() => setSettingOwner(true)}>Set owner</Button>}
            {settingOwner && <OwnerStep onBack={() => setSettingOwner(false)} onNext={setOwner} submitLabel="Set owner" />}
          </div>
        )}

        {vehicle.owner && vehicle.auth_code && !readOnly && (
          <>
            <h3 className={HEADING}>Authorization code</h3>
            <div className="flex flex-wrap items-center gap-2">
              <input value={vehicle.auth_code} readOnly className="flex-1 font-mono" />
              <CopyCodeButton value={vehicle.auth_code} />
              <a className="inline-flex h-9 items-center rounded-md border border-line-strong bg-surface px-3.5 text-sm font-medium hover:bg-surface-2 hover:no-underline" target="_blank" rel="noreferrer"
                 href={whatsAppShareUrl(vehicle.owner.phone, `Guard++ authorization code for ${vehicle.plate_number}: ${vehicle.auth_code}`)}>
                Share via WhatsApp
              </a>
            </div>
          </>
        )}

        <h3 className={HEADING}>Other authorized drivers</h3>
        {otherDrivers.length === 0 ? (
          <p className="m-0 text-[13px] text-muted">None yet — use "Add driver by code" from the Vehicles page.</p>
        ) : (
          <p className="m-0 text-[13px]"><DriverNames drivers={otherDrivers} onPick={onPickDriver} /></p>
        )}
      </div>
    </Modal>
  );
}
