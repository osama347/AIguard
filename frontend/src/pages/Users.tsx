import { useState } from "react";
import { api, type Role, type User } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { formatRelative } from "../lib/format";
import { Badge, ErrorBox, Field, Modal, PageHeader, Spinner } from "../components/ui";

const ROLES: Record<Role, { label: string; description: string }> = {
  guard: { label: "Guard", description: "Watches the live cameras, handles alerts, sees the access log. Cannot change anything else." },
  admin: { label: "Administrator", description: "Everything a guard can do, plus cameras, drivers, vehicles, users and test videos." },
};

export function UsersPage() {
  const { user: me } = useSession();
  const { data: users, error, loading, reload } = useAsync(() => api.users(), []);
  const [editing, setEditing] = useState<User | "new" | null>(null);

  const setActive = async (u: User, active: boolean) => {
    try { await api.updateUser(u.id, { active }); reload(); } catch (e) { alert((e as Error).message); }
  };
  const remove = async (u: User) => {
    if (!confirm(`Delete user "${u.username}"? Their past actions stay in the audit log.`)) return;
    try { await api.deleteUser(u.id); reload(); } catch (e) { alert((e as Error).message); }
  };

  return (
    <>
      <PageHeader
        title="Users"
        subtitle="Guards monitor the cameras and handle alerts; administrators manage the system."
        actions={<button className="btn btn-primary" onClick={() => setEditing("new")}>Add user</button>}
      />
      <ErrorBox error={error} onRetry={reload} />
      {loading && !users ? <Spinner /> : (
        <div className="card card-flush table-wrap">
          <table>
            <thead><tr><th>Name</th><th>Username</th><th>Role</th><th>Status</th><th>Last sign-in</th><th /></tr></thead>
            <tbody>
              {users?.map((u) => (
                <tr key={u.id}>
                  <td>{u.full_name || <span className="muted">—</span>}{u.id === me?.id && <span className="muted small"> (you)</span>}</td>
                  <td className="mono">{u.username}</td>
                  <td><Badge tone={u.role === "admin" ? "info" : "muted"}>{ROLES[u.role].label}</Badge></td>
                  <td>{u.active ? <Badge tone="ok">Active</Badge> : <Badge tone="bad">Disabled</Badge>}</td>
                  <td className="nowrap small">{u.last_login_at ? formatRelative(u.last_login_at) : <span className="muted">never</span>}</td>
                  <td className="nowrap">
                    <div className="row">
                      <button className="btn btn-small" onClick={() => setEditing(u)}>Edit</button>
                      {u.id !== me?.id && (
                        <>
                          <button className="btn btn-small" onClick={() => setActive(u, !u.active)}>{u.active ? "Disable" : "Enable"}</button>
                          <button className="btn btn-small btn-danger" onClick={() => remove(u)}>Delete</button>
                        </>
                      )}
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )}
      {editing && (
        <UserForm user={editing === "new" ? null : editing} isSelf={editing !== "new" && editing.id === me?.id}
                  onClose={() => setEditing(null)} onSaved={() => { setEditing(null); reload(); }} />
      )}
    </>
  );
}

function UserForm({ user, isSelf, onClose, onSaved }: {
  user: User | null; isSelf: boolean; onClose: () => void; onSaved: () => void;
}) {
  const [username, setUsername] = useState(user?.username ?? "");
  const [fullName, setFullName] = useState(user?.full_name ?? "");
  const [role, setRole] = useState<Role>(user?.role ?? "guard");
  const [password, setPassword] = useState("");
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const save = async () => {
    setSaving(true);
    setError(null);
    try {
      if (user) await api.updateUser(user.id, { full_name: fullName, role, ...(password ? { password } : {}) });
      else await api.createUser({ username, full_name: fullName, role, password });
      onSaved();
    } catch (e) {
      setError((e as Error).message);
      setSaving(false);
    }
  };

  return (
    <Modal
      title={user ? `Edit ${user.username}` : "Add user"}
      onClose={onClose}
      footer={
        <>
          <button className="btn" onClick={onClose}>Cancel</button>
          <button className="btn btn-primary" onClick={save}
                  disabled={saving || (!user && (username.trim().length < 3 || password.length < 8))}>
            {saving ? "Saving…" : "Save"}
          </button>
        </>
      }
    >
      <div className="stack">
        {!user && (
          <Field label="Username" hint="Used to sign in. Letters, digits, '.', '_' and '-'; cannot be changed later.">
            <input value={username} onChange={(e) => setUsername(e.target.value)} autoFocus autoComplete="off" />
          </Field>
        )}
        <Field label="Full name">
          <input value={fullName} onChange={(e) => setFullName(e.target.value)} maxLength={80} autoFocus={!!user} />
        </Field>
        <div className="field">
          <span className="field-label">Role</span>
          {(Object.keys(ROLES) as Role[]).map((r) => (
            <label key={r} className={`role-option ${role === r ? "selected" : ""} ${isSelf ? "disabled" : ""}`}>
              <input type="radio" name="role" checked={role === r} disabled={isSelf} onChange={() => setRole(r)} />
              <span className="stack-tight">
                <strong>{ROLES[r].label}</strong>
                <span className="muted small">{ROLES[r].description}</span>
              </span>
            </label>
          ))}
          {isSelf && <span className="field-hint">You cannot change your own role.</span>}
        </div>
        <Field label={user ? "New password (optional)" : "Password"}
               hint={user ? "Leave empty to keep the current one. Resetting signs the user out." : "At least 8 characters. Give it to the user in person."}>
          <input type="password" value={password} onChange={(e) => setPassword(e.target.value)} autoComplete="new-password" />
        </Field>
        <ErrorBox error={error} />
      </div>
    </Modal>
  );
}
