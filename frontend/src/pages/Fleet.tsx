import { Fragment, useEffect, useMemo, useState, type FormEvent } from "react";
import { useSearchParams } from "react-router-dom";
import { api, fetchProtectedImage, type Driver, type EnrollResult, type Status, type Vehicle } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { AUTHZ_STATUS, STATUS_TONE, accessCodeMessage, authorizationRequestMessage, copyToClipboard, isValidPhone, whatsAppShareUrl } from "../lib/format";
import { cn } from "../lib/cn";
import { Badge, Button, Card, Chip, Empty, ErrorBox, Fact, Facts, Field, FormSection, Modal, Notice, PageHeader, Select, Spinner } from "../components/ui";

const STATUSES: Status[] = ["active", "inactive", "blacklisted"];
const PHOTO_ERRORS: Record<string, string> = { no_face: "no face found", invalid_image: "not a valid image" };

const LINK = "text-accent hover:underline";

function Divider() {
  return <div className="h-px w-full bg-line" />;
}

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
 *  send — fetched as a blob instead. Falls back to an initials tile when there's no
 *  photo. `size` picks the CSS class (small table thumbnail vs. larger preview). */
function DriverAvatar({ photoUrl, name, size }: { photoUrl?: string | null; name?: string; size: "sm" | "lg" }) {
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

  return blobUrl
    ? <img className={cls} src={blobUrl} alt="" />
    : (
      <span className={cn(
        cls,
        "inline-grid shrink-0 place-items-center font-semibold text-faint",
        size === "sm" ? "text-[12px]" : "text-[26px]",
      )}>
        {name?.trim().slice(0, 1).toUpperCase() || "?"}
      </span>
    );
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
                    {isAdmin && <td><DriverAvatar photoUrl={d.photo_url} name={d.name} size="sm" /></td>}
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
          onSaved={onChanged}
        />
      )}
    </>
  );
}

/** Everything about a driver is changed through the single "Save changes" button:
 *  typed details, brand-new profile photo and a pending photo removal are all staged
 *  here and applied together, so there's never a half-saved driver. Face-photo
 *  enrolment and vehicle authorization are separate, explicitly-confirmed actions. */
function DriverEditor({ driver, vehicles, readOnly, onClose, onSaved }: {
  driver: Driver; vehicles: Vehicle[]; readOnly: boolean; onClose: () => void; onSaved: () => void;
}) {
  const { pushNotice } = useSession();
  const [live, setLive] = useState(driver);
  useEffect(() => setLive(driver), [driver]);
  const initial = useMemo(
    () => ({ name: driver.name, phone: driver.phone, notes: driver.notes, status: driver.status }),
    [driver],
  );
  const [form, setForm] = useState(initial);
  const [photoFile, setPhotoFile] = useState<File | null>(null);
  const [photoPreview, setPhotoPreview] = useState<string | null>(null);
  const [removePhoto, setRemovePhoto] = useState(false);
  const [faceFiles, setFaceFiles] = useState<File[]>([]);
  const [enrolled, setEnrolled] = useState<EnrollResult | null>(null);
  const [assignTo, setAssignTo] = useState<number | "">("");
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    if (!photoFile) { setPhotoPreview(null); return; }
    const url = URL.createObjectURL(photoFile);
    setPhotoPreview(url);
    return () => URL.revokeObjectURL(url);
  }, [photoFile]);

  const dirty = JSON.stringify(form) !== JSON.stringify(initial) || !!photoFile || removePhoto;

  const run = async (fn: () => Promise<unknown>, ok: string) => {
    setBusy(true);
    setError(null);
    try {
      await fn();
      pushNotice(ok);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const save = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      if (removePhoto) await api.deleteDriverPhoto(driver.id);
      if (photoFile) await api.setDriverPhoto(driver.id, photoFile);
      await api.updateDriver(driver.id, form);
      pushNotice("Driver saved");
      onSaved();
      onClose();
    } catch (err) {
      setError((err as Error).message);
      setBusy(false);
    }
  };

  const enroll = async () => {
    await run(async () => {
      const r = await api.enrollPhotos(driver.id, faceFiles);
      setEnrolled(r);
      setFaceFiles([]);
      setLive(r.driver);
      return r;
    }, "Face photos enrolled");
    onSaved();
  };

  const unassign = async (vehicleId: number) => {
    await run(async () => {
      await api.unassign(driver.id, vehicleId);
      setLive(await api.driver(driver.id));
    }, "Authorization removed");
    onSaved();
  };

  const assign = async () => {
    await run(async () => {
      const d = await api.assign(driver.id, Number(assignTo));
      setAssignTo("");
      setLive(d);
      return d;
    }, "Driver authorized");
    onSaved();
  };

  const unassigned = vehicles.filter((v) => !live.vehicles.some((dv) => dv.id === v.id));
  const shownPhoto = photoPreview ?? (removePhoto ? null : live.photo_url);

  return (
    <Modal
      title={driver.name}
      onClose={onClose}
      footer={
        readOnly ? (
          <Button onClick={onClose}>Close</Button>
        ) : (
          <>
            <Button variant="danger" disabled={busy} type="button" onClick={() => {
              if (confirm(`Delete ${driver.name}?`)) void run(async () => { await api.deleteDriver(driver.id); onClose(); onSaved(); }, "Driver deleted");
            }}>Delete</Button>
            <span className="flex-1" />
            <Button onClick={onClose} type="button" disabled={busy}>Cancel</Button>
            <Button variant="primary" form="driver-form" disabled={busy || !dirty}>{busy ? <Spinner /> : "Save changes"}</Button>
          </>
        )
      }
    >
      <form id="driver-form" className="flex flex-col gap-5" onSubmit={save}>
        <ErrorBox error={error} />

        <FormSection title="Profile photo" hint="A good front-facing portrait helps recognise the driver at the gate.">
          <div className="flex items-center gap-4">
            <DriverAvatar photoUrl={shownPhoto} name={form.name} size="lg" />
            <div className="flex min-w-0 flex-col gap-2">
              {!readOnly && (
                <>
                  <label className="flex cursor-pointer items-center gap-2 text-[13px]">
                    <input
                      type="file" accept="image/png,image/jpeg,image/webp" className="sr-only"
                      onChange={(e) => { const f = e.target.files?.[0]; if (f) { setPhotoFile(f); setRemovePhoto(false); } }}
                    />
                    <span className="rounded-md border border-line-strong bg-surface px-3 py-1.5 font-medium hover:bg-surface-2">Choose photo…</span>
                  </label>
                  {photoFile && <span className="text-[12px] text-ok">1 new photo selected — saved when you press “Save changes”.</span>}
                  {removePhoto && <span className="text-[12px] text-warn">Current photo will be removed when you press “Save changes”.</span>}
                  {live.photo_url && !photoFile && !removePhoto && (
                    <button type="button" className={cn("self-start text-[13px]", LINK)} onClick={() => setRemovePhoto(true)}>Remove current photo</button>
                  )}
                  {(photoFile || removePhoto) && (
                    <button type="button" className="self-start text-[13px] text-muted hover:underline" onClick={() => { setPhotoFile(null); setRemovePhoto(false); }}>Undo</button>
                  )}
                </>
              )}
              {readOnly && <span className="text-[12px] text-muted">{live.photo_count ? "Photo set" : "No photo"}</span>}
            </div>
          </div>
        </FormSection>

        <Divider />

        <FormSection title="Details">
          <Field label="Full name"><input value={form.name} disabled={readOnly} onChange={(e) => setForm({ ...form, name: e.target.value })} required maxLength={120} /></Field>
          <Field label="Status" hint="Blacklisted drivers are always denied and raise a critical alert.">
            <StatusSelect value={form.status} onChange={(status) => setForm({ ...form, status })} />
          </Field>
          <Field label="Phone"><input value={form.phone} disabled={readOnly} onChange={(e) => setForm({ ...form, phone: e.target.value })} maxLength={40} /></Field>
          <Field label="Notes"><textarea rows={3} value={form.notes} disabled={readOnly} onChange={(e) => setForm({ ...form, notes: e.target.value })} /></Field>
        </FormSection>

        <Divider />

        <FormSection title={`Face photos (${live.photo_count} enrolled)`} hint="Front-facing, well lit, one person per photo. Enrolment runs on its own — it doesn't need a Save.">
          {!readOnly && (
            <>
              <input type="file" accept="image/*" multiple onChange={(e) => setFaceFiles(Array.from(e.target.files ?? []))} />
              <div className="flex flex-wrap items-center gap-2">
                <Button type="button" disabled={busy || !faceFiles.length}
                  onClick={() => void enroll()}>
                  Enrol {faceFiles.length || ""} photo(s)
                </Button>
                {faceFiles.length > 0 && (
                  <button type="button" className="text-[13px] text-muted hover:underline" onClick={() => setFaceFiles([])}>Clear selection</button>
                )}
                {live.photo_count > 0 && (
                  <button type="button" className={cn("ml-auto text-[13px]", LINK)} disabled={busy}
                    onClick={() => {
                      if (confirm("Remove all enrolled face photos?")) void run(async () => { await api.clearPhotos(driver.id); setLive((l) => ({ ...l, photo_count: 0 })); }, "Face photos removed");
                    }}>
                    Remove all
                  </button>
                )}
              </div>
              <EnrollSummary result={enrolled} />
            </>
          )}
          {readOnly && <span className="text-[13px] text-muted">{live.photo_count ? `${live.photo_count} enrolled` : "None enrolled."}</span>}
        </FormSection>

        <Divider />

        <FormSection title="Authorized vehicles" hint={unassigned.length && !readOnly
          ? "Each authorized driver can drive these plates."
          : live.vehicles.length ? "Vehicles this driver is cleared to drive." : "No vehicles assigned yet."}>
          {live.vehicles.length === 0 && <p className="m-0 text-[13px] text-muted">Not authorized for any vehicle.</p>}
          <ul className="m-0 list-none p-0">
            {live.vehicles.map((v) => (
              <li key={v.id} className="flex items-center justify-between gap-3 border-b border-line py-2 last:border-b-0">
                <span className="font-mono">{v.plate_number}</span>
                {!readOnly && <Button size="sm" type="button" disabled={busy} onClick={() => void unassign(v.id)}>Remove</Button>}
              </li>
            ))}
          </ul>
          {!readOnly && unassigned.length > 0 && (
            <div className="flex flex-wrap items-end gap-2">
              <div className="flex-1">
                <Select value={assignTo === "" ? "__none__" : String(assignTo)} onValueChange={(v) => setAssignTo(v === "__none__" ? "" : Number(v))}
                        placeholder="Choose vehicle…"
                        options={[{ value: "__none__", label: "Choose vehicle…" }, ...unassigned.map((v) => ({ value: String(v.id), label: `${v.plate_number}${v.make ? ` · ${v.make} ${v.model}` : ""}` }))]} />
              </div>
              <Button type="button" disabled={busy || assignTo === ""} onClick={() => void assign()}>Authorize</Button>
            </div>
          )}
        </FormSection>
      </form>
    </Modal>
  );
}

/** Redeems an owner's authorization code to add a new authorized driver to their vehicle.
 *  Reused standalone (code typed in, e.g. from the Vehicles page) and inside the
 *  registration wizard, where the code was just generated and is pre-filled. */
function DriverAuthorizeForm({ code: initialCode, codeReadOnly, onDone }: {
  code?: string; codeReadOnly?: boolean; onDone: (d: Driver) => void;
}) {
  const { pushNotice } = useSession();
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
      pushNotice(`Driver ${d.name} added`);
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
  const d = driver.data;
  return (
    <Modal title="Driver profile" onClose={onClose}>
      <ErrorBox error={driver.error} onRetry={driver.reload} />
      {!d ? <div className="grid place-items-center py-12"><Spinner /></div> : (
        <div className="flex flex-col gap-5">
          <div className="flex items-center gap-4">
            <DriverAvatar photoUrl={d.photo_url} name={d.name} size="lg" />
            <div className="flex min-w-0 flex-col gap-0.5">
              <div className="flex flex-wrap items-center gap-2">
                <h2 className="text-[16px] font-semibold tracking-tight">{d.name}</h2>
                <Badge tone={STATUS_TONE[d.status]}>{d.status}</Badge>
              </div>
              <div className="text-[12.5px] text-muted">{d.is_owner ? "Vehicle owner" : "Authorized driver"}</div>
              {d.phone && <div className="font-mono text-[13px]">{d.phone}</div>}
            </div>
          </div>

          <Divider />

          <FormSection title="Vehicles" hint={d.vehicles.length ? "Plates this driver is cleared to drive." : "Not authorized for any vehicle."}>
            {d.vehicles.length === 0 ? <p className="m-0 text-[13px] text-muted">—</p> : (
              <ul className="m-0 flex list-none flex-col gap-2 p-0">
                {d.vehicles.map((v) => (
                  <li key={v.id}>
                    <Chip>{v.plate_number}</Chip>
                  </li>
                ))}
              </ul>
            )}
          </FormSection>

          <Divider />

          <FormSection title="Profile">
            <Facts>
              <Fact label="Phone">{d.phone || "—"}</Fact>
              <Fact label="Face photos">{d.photo_count ? `${d.photo_count} enrolled` : <Badge tone="warn">none</Badge>}</Fact>
              {d.notes && <Fact label="Notes">{d.notes}</Fact>}
            </Facts>
          </FormSection>
        </div>
      )}
    </Modal>
  );
}

/** Copies a code with a brief "Copied!" confirmation; works on plain HTTP (see copyToClipboard). */
function CopyCodeButton({ value }: { value: string }) {
  const [copied, setCopied] = useState(false);
  return (
    <Button type="button" onClick={async () => {
      if (await copyToClipboard(value)) { setCopied(true); setTimeout(() => setCopied(false), 1500); }
    }}>
      {copied ? "Copied!" : "Copy"}
    </Button>
  );
}

/** Name + mandatory phone/WhatsApp + notes: the owner-specific fields, reused by the
 *  registration wizard (step 2) and by backfilling a legacy vehicle's owner. Only ever
 *  submitted through an explicit button — nothing happens on typing or tabbing away. */
function OwnerStep({ initial, onBack, onNext, submitLabel }: {
  initial?: { name: string; phone: string; notes: string };
  onBack?: () => void; onNext: (owner: { name: string; phone: string; notes: string }) => void; submitLabel: string;
}) {
  const [name, setName] = useState(initial?.name ?? "");
  const [phone, setPhone] = useState(initial?.phone ?? "");
  const [notes, setNotes] = useState(initial?.notes ?? "");
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
      {touched && !name.trim() && <ErrorBox error="Enter the owner's name." />}
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
  const { pushNotice } = useSession();
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const submit = async (owner: { name: string; phone: string; notes: string }) => {
    setBusy(true);
    setError(null);
    try {
      await api.setVehicleOwner(vehicle.id, owner);
      pushNotice("Owner set");
      onSaved();
    } catch (err) {
      setError((err as Error).message);
      setBusy(false);
    }
  };
  return (
    <Modal title={`Set owner for ${vehicle.plate_number}`} onClose={onClose}>
      <div className="flex flex-col gap-3">
        <ErrorBox error={error} />
        <p className="m-0 text-[13px] text-muted">The owner is auto-enrolled and authorized to drive this vehicle.</p>
        {busy ? <Spinner /> : <OwnerStep onNext={submit} submitLabel="Set owner" />}
      </div>
    </Modal>
  );
}

type WizardStep = "vehicle" | "owner" | "photos" | "drivers";

const WIZARD_STEPS: { key: WizardStep; label: string }[] = [
  { key: "vehicle", label: "Vehicle" },
  { key: "owner", label: "Owner" },
  { key: "photos", label: "Photos" },
  { key: "drivers", label: "Drivers" },
];

/** Register a vehicle → its owner → (optionally) further authorized drivers, in that
 *  order. Vehicle + owner are submitted together as one atomic request on the owner
 *  step; only the vehicle fields live in local state until then. */
export function RegisterVehicleWizard({ initialPlate, onClose, onDone }: {
  initialPlate?: string; onClose: () => void; onDone: () => void;
}) {
  const { pushNotice } = useSession();
  const [step, setStep] = useState<WizardStep>("vehicle");
  const [vehicleForm, setVehicleForm] = useState({ plate_number: initialPlate ?? "", make: "", model: "", color: "" });
  const [ownerInfo, setOwnerInfo] = useState<{ name: string; phone: string; notes: string } | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [created, setCreated] = useState<Vehicle | null>(null);
  const [addedDrivers, setAddedDrivers] = useState<Driver[]>([]);
  const [photoFiles, setPhotoFiles] = useState<File[]>([]);
  const [enrolled, setEnrolled] = useState<EnrollResult | null>(null);
  const [busyPhotos, setBusyPhotos] = useState(false);
  const setV = (k: keyof typeof vehicleForm) => (e: { target: { value: string } }) => setVehicleForm({ ...vehicleForm, [k]: e.target.value });

  const stepIndex = WIZARD_STEPS.findIndex((s) => s.key === step);

  const submitVehicleStep = (e: FormEvent) => {
    e.preventDefault();
    if (!vehicleForm.plate_number.trim()) return;
    setStep("owner");
  };

  const submitOwner = async (owner: { name: string; phone: string; notes: string }) => {
    setBusy(true);
    setError(null);
    setOwnerInfo(owner);
    try {
      const vehicle = await api.createVehicle({ ...vehicleForm, owner });
      setCreated(vehicle);
      pushNotice(`${vehicle.plate_number} registered`);
      setStep("photos");
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const enrollOwnerPhotos = async () => {
    if (!created?.owner || !photoFiles.length) return;
    setBusyPhotos(true);
    setError(null);
    try {
      const r = await api.enrollPhotos(created.owner.id, photoFiles);
      setEnrolled(r);
      setPhotoFiles([]);
      pushNotice(`${r.enrolled} owner photo${r.enrolled === 1 ? "" : "s"} enrolled`);
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusyPhotos(false);
    }
  };

  const goTo = (i: number) => {
    // Only allow stepping back while still on the pre-registration steps, and never
    // past the created vehicle (owner data is kept so going back doesn't lose it).
    if (created || i < 0 || i > stepIndex) return;
    setStep(WIZARD_STEPS[i].key as WizardStep);
  };

  return (
    <Modal title="Register a vehicle" onClose={onClose}>
      <div className="flex flex-col gap-5">
        <div className="flex items-center gap-1.5">
          {WIZARD_STEPS.map((s, i) => (
            <Fragment key={s.key}>
              {i > 0 && <div className={cn("h-px flex-1", i <= stepIndex ? "bg-ink" : "bg-line")} />}
              <button
                type="button"
                disabled={i >= stepIndex}
                onClick={() => goTo(i)}
                className={cn(
                  "flex items-center gap-1.5 text-[12px] font-medium transition-colors",
                  i === stepIndex ? "text-text" : "text-muted hover:text-text",
                )}
              >
                <span className={cn(
                  "grid size-5 place-items-center rounded-full text-[10.5px] font-semibold",
                  i <= stepIndex ? "bg-ink text-ink-text" : "bg-surface-2 text-muted",
                )}>
                  {i + 1}
                </span>
                <span className={cn(i < stepIndex && "hidden sm:inline")}>{s.label}</span>
              </button>
            </Fragment>
          ))}
        </div>

        <ErrorBox error={error} />

        {step === "vehicle" && (
          <form onSubmit={submitVehicleStep} className="flex flex-col gap-3">
            <Field label="Plate number" hint="Matched ignoring spaces, dashes and case — e.g. “ABC 123” and “abc-123” are the same plate.">
              <input value={vehicleForm.plate_number} onChange={setV("plate_number")} required autoFocus className="font-mono" placeholder="ABC 123" />
            </Field>
            <div className="grid gap-3 sm:grid-cols-2">
              <Field label="Make"><input value={vehicleForm.make} onChange={setV("make")} maxLength={40} /></Field>
              <Field label="Model"><input value={vehicleForm.model} onChange={setV("model")} maxLength={40} /></Field>
            </div>
            <Field label="Color"><input value={vehicleForm.color} onChange={setV("color")} maxLength={30} /></Field>
            <div className="flex items-center"><span className="flex-1" /><Button variant="primary">Next: Owner</Button></div>
          </form>
        )}

        {step === "owner" && (
          <div className="flex flex-col gap-3">
            <p className="m-0 text-[13px] text-muted">The owner is auto-enrolled and authorized to drive {vehicleForm.plate_number}. Vehicle and owner are registered together in one step.</p>
            {busy ? <Spinner /> : (
              <OwnerStep initial={ownerInfo ?? undefined} onBack={() => setStep("vehicle")} onNext={submitOwner} submitLabel="Register vehicle" />
            )}
          </div>
        )}

        {step === "photos" && created && (
          <div className="flex flex-col gap-5">
            <Notice tone="ok">
              <strong>{created.plate_number} registered.</strong> Owner: {created.owner?.name}.
            </Notice>

            <FormSection
              title="Owner face photos"
              hint={`So ${created.owner?.name ?? "the owner"} can unlock the gate by face. Front-facing, well lit, one person per photo. Adds first, then enrols — photos aren't submitted until you press Enrol. You can add more later from their profile.`}>
              <input type="file" accept="image/*" multiple onChange={(e) => setPhotoFiles(Array.from(e.target.files ?? []))} />
              <div className="flex flex-wrap items-center gap-2">
                <Button variant="primary" type="button" disabled={busyPhotos || !photoFiles.length} onClick={() => void enrollOwnerPhotos()}>
                  {busyPhotos ? <Spinner /> : <>Enrol {photoFiles.length || ""} photo{photoFiles.length === 1 ? "" : "s"}</>}
                </Button>
                {photoFiles.length > 0 && (
                  <button type="button" className="text-[13px] text-muted hover:underline" onClick={() => setPhotoFiles([])}>Clear selection</button>
                )}
              </div>
              <EnrollSummary result={enrolled} />
            </FormSection>

            <Divider />
            <div className="flex flex-wrap items-center gap-2">
              <span className="flex-1 text-[13px] text-muted">Photos are optional — skip for now and add them later.</span>
              <Button variant="primary" onClick={() => setStep("drivers")}>Next: Drivers</Button>
            </div>
          </div>
        )}

        {step === "drivers" && created && (
          <div className="flex flex-col gap-5">
            {created.auth_code && (
              <FormSection title="Authorization code" hint="Share this with anyone the owner authorizes to drive — an admin can add them later with it, without the owner present.">
                <div className="flex flex-wrap items-end gap-2">
                  <input value={created.auth_code} readOnly className="flex-1 font-mono" />
                  <CopyCodeButton value={created.auth_code} />
                  {created.owner?.phone && (
                    <a className="inline-flex h-9 items-center rounded-md border border-line-strong bg-surface px-3.5 text-sm font-medium hover:bg-surface-2 hover:no-underline" target="_blank" rel="noreferrer"
                       href={whatsAppShareUrl(created.owner.phone, accessCodeMessage(created.plate_number, created.auth_code))}>
                      Share via WhatsApp
                    </a>
                  )}
                </div>
              </FormSection>
            )}

            {addedDrivers.length > 0 && (
              <> 
                <Divider />
                <FormSection title={`Added so far (${addedDrivers.length})`}>
                  <ul className="m-0 list-none p-0">
                    {addedDrivers.map((d) => <li key={d.id} className="border-b border-line py-2 last:border-b-0">{d.name}</li>)}
                  </ul>
                </FormSection>
              </>
            )}

            <Divider />
            <FormSection title="Add another driver (optional)" hint="Fine to skip — the owner can share the code later.">
              <DriverAuthorizeForm code={created.auth_code ?? ""} codeReadOnly onDone={(d) => setAddedDrivers((a) => [...a, d])} />
            </FormSection>

            <div className="flex items-center"><span className="flex-1" /><Button variant="primary" onClick={onDone}>Done</Button></div>
          </div>
        )}
      </div>
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

/** Lets an admin ask the vehicle's owner to approve a new driver remotely: the owner
 *  gets a one-tap Approve/Deny link (shared over WhatsApp) instead of handing out the
 *  reusable authorization code. The raw link is only ever shown once, right after the
 *  request is created — it isn't retrievable afterward, only cancellable. */
function AuthorizationRequests({ vehicle, owner }: { vehicle: Vehicle; owner: Vehicle["owner"] }) {
  const { pushNotice } = useSession();
  const list = useAsync(() => api.authorizations({ vehicle_id: vehicle.id }), [vehicle.id]);
  const [name, setName] = useState("");
  const [phone, setPhone] = useState("");
  const [photos, setPhotos] = useState<File[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [justCreated, setJustCreated] = useState<{ link: string; driverName: string } | null>(null);

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      const fd = new FormData();
      fd.append("vehicle_id", String(vehicle.id));
      fd.append("driver_name", name);
      fd.append("driver_phone", phone);
      photos.forEach((f) => fd.append("photos", f, f.name));
      const r = await api.createAuthorization(fd);
      setJustCreated({ link: r.link, driverName: r.authorization.driver_name });
      setName(""); setPhone(""); setPhotos([]);
      pushNotice("Request sent — share the link with the owner");
      list.reload();
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const cancel = async (id: number) => {
    setError(null);
    try {
      await api.cancelAuthorization(id);
      list.reload();
      pushNotice("Request cancelled");
    } catch (err) {
      setError((err as Error).message);
    }
  };

  return (
    <FormSection title="Request owner approval" hint="Ask the owner to approve a new driver remotely instead of using the authorization code — they get a one-tap Approve/Deny link. Not needed if the owner is present (use the code above instead).">
      <ErrorBox error={error} />
      <form onSubmit={submit} className="flex flex-col gap-3">
        <div className="grid gap-3 sm:grid-cols-2">
          <Field label="Driver's full name"><input value={name} onChange={(e) => setName(e.target.value)} required maxLength={120} /></Field>
          <Field label="Driver's phone (optional)"><input value={phone} onChange={(e) => setPhone(e.target.value)} maxLength={40} /></Field>
        </div>
        <Field label="Face photos (optional)" hint="Staged until the owner approves, then enrolled automatically.">
          <input type="file" accept="image/*" multiple onChange={(e) => setPhotos(Array.from(e.target.files ?? []))} />
        </Field>
        <div className="flex items-center"><span className="flex-1" /><Button variant="primary" disabled={busy || !name.trim()}>{busy ? <Spinner /> : "Send request"}</Button></div>
      </form>

      {justCreated && (
        <Notice tone="ok">
          <strong>Request sent for {justCreated.driverName}.</strong> Share this link with the owner — it's only shown once.
          <div className="mt-2 flex flex-wrap items-center gap-2">
            <input value={justCreated.link} readOnly className="min-w-0 flex-1 font-mono text-[12px]" />
            <CopyCodeButton value={justCreated.link} />
            {owner?.phone && (
              <a className="inline-flex h-9 items-center rounded-md border border-line-strong bg-surface px-3.5 text-sm font-medium hover:bg-surface-2 hover:no-underline" target="_blank" rel="noreferrer"
                 href={whatsAppShareUrl(owner.phone, authorizationRequestMessage(vehicle.plate_number, justCreated.driverName, justCreated.link))}>
                Share via WhatsApp
              </a>
            )}
          </div>
        </Notice>
      )}

      {!!list.data?.length && (
        <ul className="m-0 list-none p-0">
          {list.data.map((a) => (
            <li key={a.id} className="flex flex-wrap items-center justify-between gap-2 border-b border-line py-2 last:border-b-0 text-[13px]">
              <span><strong className="font-medium">{a.driver_name}</strong>{a.driver_phone && <span className="ml-1.5 font-mono text-muted">{a.driver_phone}</span>}</span>
              <span className="flex items-center gap-2">
                <Badge tone={AUTHZ_STATUS[a.status]?.tone ?? "muted"}>{AUTHZ_STATUS[a.status]?.label ?? a.status}</Badge>
                {a.status === "pending" && <Button size="sm" type="button" onClick={() => void cancel(a.id)}>Cancel</Button>}
              </span>
            </li>
          ))}
        </ul>
      )}
    </FormSection>
  );
}

function VehicleEditor({ vehicle, readOnly, onClose, onSaved, onPickDriver }: {
  vehicle: Vehicle; readOnly: boolean; onClose: () => void; onSaved: () => void; onPickDriver: (id: number) => void;
}) {
  const { pushNotice } = useSession();
  const initial = useMemo(
    () => ({ plate_number: vehicle.plate_number, make: vehicle.make, model: vehicle.model, color: vehicle.color, status: vehicle.status }),
    [vehicle],
  );
  const [form, setForm] = useState(initial);
  const [owner, setOwner] = useState(vehicle.owner);
  const [settingOwner, setSettingOwner] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const dirty = JSON.stringify(form) !== JSON.stringify(initial);
  const set = (k: keyof typeof form) => (e: { target: { value: string } }) => setForm({ ...form, [k]: e.target.value });

  const save = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      await api.updateVehicle(vehicle.id, form);
      pushNotice("Vehicle saved");
      onSaved();
    } catch (err) {
      setError((err as Error).message);
      setBusy(false);
    }
  };

  const remove = async () => {
    if (!confirm(`Delete vehicle ${vehicle.plate_number}?`)) return;
    setBusy(true);
    setError(null);
    try {
      await api.deleteVehicle(vehicle.id);
      pushNotice("Vehicle deleted");
      onSaved();
    } catch (err) {
      setError((err as Error).message);
      setBusy(false);
    }
  };

  const setOwnerNow = async (o: { name: string; phone: string; notes: string }) => {
    setBusy(true);
    setError(null);
    try {
      await api.setVehicleOwner(vehicle.id, o);
      setOwner({ id: 0, name: o.name, phone: o.phone });
      setSettingOwner(false);
      pushNotice("Owner set");
    } catch (err) {
      setError((err as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const otherDrivers = vehicle.drivers.filter((d) => d.id !== vehicle.owner?.id);

  return (
    <Modal
      title={vehicle.plate_number}
      onClose={onClose}
      footer={
        readOnly ? (
          <Button onClick={onClose}>Close</Button>
        ) : (
          <>
            <Button variant="danger" type="button" disabled={busy} onClick={() => void remove()}>Delete</Button>
            <span className="flex-1" />
            <Button type="button" disabled={busy} onClick={onClose}>Cancel</Button>
            <Button variant="primary" form="vehicle-form" disabled={busy || !dirty}>{busy ? <Spinner /> : "Save changes"}</Button>
          </>
        )
      }
    >
      <form id="vehicle-form" className="flex flex-col gap-5" onSubmit={save}>
        <ErrorBox error={error} />

        <FormSection title="Details" hint="Plates are matched ignoring spaces, dashes and case.">
          <Field label="Plate number"><input value={form.plate_number} onChange={set("plate_number")} required disabled={readOnly} autoFocus className="font-mono" placeholder="ABC 123" /></Field>
          <div className="grid gap-3 sm:grid-cols-2">
            <Field label="Make"><input value={form.make} onChange={set("make")} disabled={readOnly} maxLength={40} /></Field>
            <Field label="Model"><input value={form.model} onChange={set("model")} disabled={readOnly} maxLength={40} /></Field>
          </div>
          <Field label="Color"><input value={form.color} onChange={set("color")} disabled={readOnly} maxLength={30} /></Field>
          <Field label="Status" hint="Blacklisted vehicles are always denied.">
            <StatusSelect value={form.status} onChange={(status) => setForm({ ...form, status })} />
          </Field>
        </FormSection>

        <Divider />

        <FormSection title="Owner" hint="The owner is auto-enrolled and holds the authorization code for this vehicle.">
          {owner ? (
            <ul className="m-0 flex list-none flex-col gap-1.5 p-0">
              <li className="flex flex-wrap items-center gap-2 text-[13px]">
                <button type="button" className={LINK} onClick={() => onPickDriver(owner.id)}>{owner.name}</button>
                {owner.phone && <span className="font-mono text-muted">{owner.phone}</span>}
              </li>
            </ul>
          ) : (
            <p className="m-0 text-[13px] text-muted">No owner set — this vehicle was registered before this feature existed.</p>
          )}
          {vehicle.auth_code && (
            <div className="flex flex-wrap items-end gap-2">
              <input value={vehicle.auth_code} readOnly className="flex-1 font-mono" />
              {!readOnly && <CopyCodeButton value={vehicle.auth_code} />}
              {owner?.phone && vehicle.auth_code && (
                <a className="inline-flex h-9 items-center rounded-md border border-line-strong bg-surface px-3.5 text-sm font-medium hover:bg-surface-2 hover:no-underline" target="_blank" rel="noreferrer"
                   href={whatsAppShareUrl(owner.phone, accessCodeMessage(vehicle.plate_number, vehicle.auth_code))}>
                  Share via WhatsApp
                </a>
              )}
            </div>
          )}
          {!readOnly && !owner && !settingOwner && <Button type="button" disabled={busy} onClick={() => setSettingOwner(true)}>Set owner</Button>}
          {!readOnly && settingOwner && <OwnerStep onBack={() => setSettingOwner(false)} onNext={setOwnerNow} submitLabel="Set owner" />}
        </FormSection>

        {!readOnly && owner && (
          <>
            <Divider />
            <AuthorizationRequests vehicle={vehicle} owner={owner} />
          </>
        )}

        <Divider />

        <FormSection title="Other authorized drivers" hint={otherDrivers.length ? "Click a name to open their profile." : "None yet — use “Add driver by code” to redeem the owner's code."}>
          <p className="m-0 text-[13px]"><DriverNames drivers={otherDrivers} onPick={onPickDriver} /></p>
        </FormSection>
      </form>
    </Modal>
  );
}