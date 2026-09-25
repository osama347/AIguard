import { useMemo, useState, type FormEvent } from "react";
import { api, type Driver, type EnrollResult, type Status, type Vehicle } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { STATUS_TONE } from "../lib/format";
import { Badge, Empty, ErrorBox, Field, Modal, PageHeader, Spinner } from "../components/ui";

const STATUSES: Status[] = ["active", "inactive", "blacklisted"];
const PHOTO_ERRORS: Record<string, string> = { no_face: "no face found", invalid_image: "not a valid image" };

function useFilter<T>(items: T[] | undefined, text: (t: T) => string) {
  const [q, setQ] = useState("");
  const filtered = useMemo(
    () => (items ?? []).filter((i) => text(i).toLowerCase().includes(q.trim().toLowerCase())),
    [items, q, text],
  );
  return { q, setQ, filtered };
}

function StatusSelect({ value, onChange }: { value: Status; onChange: (s: Status) => void }) {
  return (
    <select value={value} onChange={(e) => onChange(e.target.value as Status)}>
      {STATUSES.map((s) => <option key={s} value={s}>{s}</option>)}
    </select>
  );
}

function EnrollSummary({ result }: { result: EnrollResult | null }) {
  if (!result) return null;
  const failed = result.photos.filter((p) => !p.enrolled);
  return (
    <div className={failed.length ? "notice" : "notice notice-ok"}>
      {result.enrolled} photo(s) enrolled.
      {failed.map((p) => <div key={p.filename} className="small">{p.filename}: {PHOTO_ERRORS[p.error ?? ""] ?? p.error}</div>)}
    </div>
  );
}

// ====================================================================== drivers

export function DriversPage() {
  const { user } = useSession();
  const isAdmin = user?.role === "admin";
  const drivers = useAsync(() => api.drivers(), []);
  const vehicles = useAsync(() => api.vehicles(), []);
  const [creating, setCreating] = useState(false);
  const [editing, setEditing] = useState<Driver | null>(null);
  const textOf = useMemo(() => (d: Driver) => `${d.name} ${d.phone} ${d.vehicles.map((v) => v.plate_number).join(" ")}`, []);
  const { q, setQ, filtered } = useFilter(drivers.data, textOf);

  const refresh = () => { drivers.reload(); vehicles.reload(); };

  return (
    <>
      <PageHeader
        title="Drivers"
        subtitle="People allowed to drive fleet vehicles. Enroll 2–5 clear face photos per driver."
        actions={isAdmin && <button className="btn btn-primary" onClick={() => setCreating(true)}>Add driver</button>}
      />
      <ErrorBox error={drivers.error} onRetry={drivers.reload} />
      {drivers.loading && !drivers.data ? <Spinner /> : !drivers.data?.length ? (
        <Empty title="No drivers yet">Add a driver and enroll their face photos.</Empty>
      ) : (
        <div className="card card-flush">
          <div className="table-tools"><input placeholder="Search drivers…" value={q} onChange={(e) => setQ(e.target.value)} /></div>
          <div className="table-wrap">
            <table>
              <thead><tr><th>Name</th><th>Status</th><th>Face photos</th><th>Vehicles</th><th>Phone</th></tr></thead>
              <tbody>
                {filtered.map((d) => (
                  <tr key={d.id} className="clickable" onClick={() => setEditing(d)}>
                    <td><strong>{d.name}</strong></td>
                    <td><Badge tone={STATUS_TONE[d.status]}>{d.status}</Badge></td>
                    <td>{d.photo_count ? d.photo_count : <Badge tone="warn">none</Badge>}</td>
                    <td className="mono">{d.vehicles.map((v) => v.plate_number).join(", ") || "—"}</td>
                    <td>{d.phone || "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}
      {creating && <DriverCreate onClose={() => setCreating(false)} onDone={(d) => { setCreating(false); refresh(); setEditing(d); }} />}
      {editing && (
        <DriverEditor
          driver={editing}
          vehicles={vehicles.data ?? []}
          readOnly={!isAdmin}
          onClose={() => setEditing(null)}
          onChanged={(d) => { if (d) setEditing(d); refresh(); }}
        />
      )}
    </>
  );
}

function DriverCreate({ onClose, onDone }: { onClose: () => void; onDone: (d: Driver) => void }) {
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
      let d = await api.createDriver({ name, phone });
      if (photos.length) d = (await api.enrollPhotos(d.id, photos)).driver;
      onDone(d);
    } catch (err) {
      setError((err as Error).message);
      setBusy(false);
    }
  };

  return (
    <Modal title="Add driver" onClose={onClose}
      footer={<><button className="btn" onClick={onClose}>Cancel</button>
               <button className="btn btn-primary" form="driver-create" disabled={busy}>{busy ? <Spinner /> : "Add driver"}</button></>}>
      <form id="driver-create" onSubmit={submit} className="stack">
        <ErrorBox error={error} />
        <Field label="Full name"><input value={name} onChange={(e) => setName(e.target.value)} required autoFocus maxLength={120} /></Field>
        <Field label="Phone (optional)"><input value={phone} onChange={(e) => setPhone(e.target.value)} maxLength={40} /></Field>
        <Field label="Face photos" hint="Front-facing, well lit, one person per photo. You can add more later.">
          <input type="file" accept="image/*" multiple onChange={(e) => setPhotos(Array.from(e.target.files ?? []))} />
        </Field>
      </form>
    </Modal>
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
      <div className="grid-2">
        <form className="stack" onSubmit={(e) => { e.preventDefault(); run(() => api.updateDriver(driver.id, form)); }}>
          <h3>Details</h3>
          <Field label="Full name"><input value={form.name} disabled={readOnly} onChange={(e) => setForm({ ...form, name: e.target.value })} required /></Field>
          <Field label="Status" hint="Blacklisted drivers are always denied and raise a critical alert.">
            <StatusSelect value={form.status} onChange={(status) => setForm({ ...form, status })} />
          </Field>
          <Field label="Phone"><input value={form.phone} disabled={readOnly} onChange={(e) => setForm({ ...form, phone: e.target.value })} /></Field>
          <Field label="Notes"><textarea rows={3} value={form.notes} disabled={readOnly} onChange={(e) => setForm({ ...form, notes: e.target.value })} /></Field>
          {!readOnly && <div className="row">
            <button className="btn btn-primary" disabled={busy}>Save</button>
            <button type="button" className="btn btn-danger" disabled={busy}
              onClick={() => confirm(`Delete ${driver.name}?`) && run(async () => { await api.deleteDriver(driver.id); onClose(); })}>Delete</button>
          </div>}
        </form>

        <div className="stack">
          <h3>Face photos <span className="muted">({driver.photo_count} enrolled)</span></h3>
          {!readOnly && (
            <>
              <input type="file" accept="image/*" multiple onChange={(e) => setPhotos(Array.from(e.target.files ?? []))} />
              <div className="row">
                <button className="btn" disabled={busy || !photos.length}
                  onClick={() => run(async () => { const r = await api.enrollPhotos(driver.id, photos); setEnrolled(r); setPhotos([]); return r.driver; })}>
                  Enroll {photos.length || ""} photo(s)
                </button>
                {driver.photo_count > 0 && (
                  <button className="btn btn-link" disabled={busy}
                    onClick={() => confirm("Remove all enrolled face photos?") && run(async () => { await api.clearPhotos(driver.id); return { ...driver, photo_count: 0 }; })}>
                    Remove all
                  </button>
                )}
              </div>
              <EnrollSummary result={enrolled} />
            </>
          )}

          <h3>Authorized vehicles</h3>
          {driver.vehicles.length === 0 && <p className="muted">Not authorized for any vehicle.</p>}
          <ul className="list">
            {driver.vehicles.map((v) => (
              <li key={v.id} className="list-row">
                <span className="mono">{v.plate_number}</span>
                {!readOnly && <button className="btn btn-small" disabled={busy}
                  onClick={() => run(async () => { await api.unassign(driver.id, v.id); return { ...driver, vehicles: driver.vehicles.filter((x) => x.id !== v.id) }; })}>
                  Remove</button>}
              </li>
            ))}
          </ul>
          {!readOnly && unassigned.length > 0 && (
            <div className="row">
              <select value={assignTo} onChange={(e) => setAssignTo(e.target.value ? Number(e.target.value) : "")}>
                <option value="">Choose vehicle…</option>
                {unassigned.map((v) => <option key={v.id} value={v.id}>{v.plate_number}{v.make ? ` · ${v.make} ${v.model}` : ""}</option>)}
              </select>
              <button className="btn" disabled={busy || assignTo === ""}
                onClick={() => run(async () => { const d = await api.assign(driver.id, Number(assignTo)); setAssignTo(""); return d; })}>Authorize</button>
            </div>
          )}
        </div>
      </div>
    </Modal>
  );
}

// ====================================================================== vehicles

export function VehiclesPage() {
  const { user } = useSession();
  const isAdmin = user?.role === "admin";
  const vehicles = useAsync(() => api.vehicles(), []);
  const [editing, setEditing] = useState<Vehicle | "new" | null>(null);
  const textOf = useMemo(() => (v: Vehicle) => `${v.plate_number} ${v.plate_normalized} ${v.make} ${v.model} ${v.color}`, []);
  const { q, setQ, filtered } = useFilter(vehicles.data, textOf);

  return (
    <>
      <PageHeader
        title="Vehicles"
        subtitle="Registered plates. Plates are matched ignoring spaces, dashes and case."
        actions={isAdmin && <button className="btn btn-primary" onClick={() => setEditing("new")}>Add vehicle</button>}
      />
      <ErrorBox error={vehicles.error} onRetry={vehicles.reload} />
      {vehicles.loading && !vehicles.data ? <Spinner /> : !vehicles.data?.length ? (
        <Empty title="No vehicles yet">Register a vehicle by its plate number.</Empty>
      ) : (
        <div className="card card-flush">
          <div className="table-tools"><input placeholder="Search plates…" value={q} onChange={(e) => setQ(e.target.value)} /></div>
          <div className="table-wrap">
            <table>
              <thead><tr><th>Plate</th><th>Status</th><th>Vehicle</th><th>Authorized drivers</th></tr></thead>
              <tbody>
                {filtered.map((v) => (
                  <tr key={v.id} className="clickable" onClick={() => setEditing(v)}>
                    <td className="mono"><strong>{v.plate_number}</strong></td>
                    <td><Badge tone={STATUS_TONE[v.status]}>{v.status}</Badge></td>
                    <td>{[v.color, v.make, v.model].filter(Boolean).join(" ") || "—"}</td>
                    <td>{v.drivers.map((d) => d.name).join(", ") || "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </div>
      )}
      {editing && (
        <VehicleEditor vehicle={editing === "new" ? null : editing} readOnly={!isAdmin}
          onClose={() => setEditing(null)} onSaved={() => { setEditing(null); vehicles.reload(); }} />
      )}
    </>
  );
}

function VehicleEditor({ vehicle, readOnly, onClose, onSaved }: {
  vehicle: Vehicle | null; readOnly: boolean; onClose: () => void; onSaved: () => void;
}) {
  const [form, setForm] = useState({
    plate_number: vehicle?.plate_number ?? "", make: vehicle?.make ?? "", model: vehicle?.model ?? "",
    color: vehicle?.color ?? "", status: (vehicle?.status ?? "active") as Status,
  });
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const set = (k: keyof typeof form) => (e: { target: { value: string } }) => setForm({ ...form, [k]: e.target.value });

  const submit = async (e: FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      if (vehicle) await api.updateVehicle(vehicle.id, form);
      else await api.createVehicle(form);
      onSaved();
    } catch (err) {
      setError((err as Error).message);
      setBusy(false);
    }
  };

  const remove = async () => {
    if (!vehicle || !confirm(`Delete vehicle ${vehicle.plate_number}?`)) return;
    try { await api.deleteVehicle(vehicle.id); onSaved(); } catch (err) { setError((err as Error).message); }
  };

  return (
    <Modal title={vehicle ? vehicle.plate_number : "Add vehicle"} onClose={onClose}
      footer={!readOnly && <>
        {vehicle && <button className="btn btn-danger" onClick={remove} disabled={busy}>Delete</button>}
        <span className="spacer" />
        <button className="btn" onClick={onClose}>Cancel</button>
        <button className="btn btn-primary" form="vehicle-form" disabled={busy}>{busy ? <Spinner /> : "Save"}</button>
      </>}>
      <form id="vehicle-form" onSubmit={submit} className="stack">
        <ErrorBox error={error} />
        <Field label="Plate number"><input value={form.plate_number} onChange={set("plate_number")} required disabled={readOnly} autoFocus className="mono" /></Field>
        <div className="grid-3">
          <Field label="Make"><input value={form.make} onChange={set("make")} disabled={readOnly} /></Field>
          <Field label="Model"><input value={form.model} onChange={set("model")} disabled={readOnly} /></Field>
          <Field label="Color"><input value={form.color} onChange={set("color")} disabled={readOnly} /></Field>
        </div>
        <Field label="Status" hint="Blacklisted vehicles are always denied.">
          <StatusSelect value={form.status} onChange={(status) => setForm({ ...form, status })} />
        </Field>
        {vehicle && (
          <p className="muted small">
            Authorized drivers: {vehicle.drivers.map((d) => d.name).join(", ") || "none"} — manage them from the Drivers page.
          </p>
        )}
      </form>
    </Modal>
  );
}
