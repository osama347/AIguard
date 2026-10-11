import { useState } from "react";
import { api, type Role, type User } from "../api/client";
import { useAsync } from "../lib/useAsync";
import { useSession } from "../session";
import { formatRelative } from "../lib/format";
import { cn } from "../lib/cn";
import { Badge, Button, Card, ErrorBox, Field, Modal, PageHeader, Spinner } from "../components/ui";

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
        actions={<Button variant="primary" onClick={() => setEditing("new")}>Add user</Button>}
      />
      <ErrorBox error={error} onRetry={reload} />
      {loading && !users ? <div className="grid place-items-center py-16"><Spinner /></div> : (
        <Card flush>
          <div className="overflow-x-auto">
            <table>
              <thead><tr><th>Name</th><th>Username</th><th>Role</th><th>Status</th><th>Last sign-in</th><th /></tr></thead>
              <tbody>
                {users?.map((u) => (
                  <tr key={u.id}>
                    <td>{u.full_name || <span className="text-muted">—</span>}{u.id === me?.id && <span className="text-[12px] text-muted"> (you)</span>}</td>
                    <td className="font-mono">{u.username}</td>
                    <td><Badge tone={u.role === "admin" ? "info" : "muted"}>{ROLES[u.role].label}</Badge></td>
                    <td>{u.active ? <Badge tone="ok">Active</Badge> : <Badge tone="bad">Disabled</Badge>}</td>
                    <td className="whitespace-nowrap text-[12px] text-muted">{u.last_login_at ? formatRelative(u.last_login_at) : "never"}</td>
                    <td className="whitespace-nowrap">
                      <div className="flex justify-end gap-2">
                        <Button size="sm" onClick={() => setEditing(u)}>Edit</Button>
                        {u.id !== me?.id && (
                          <>
                            <Button size="sm" onClick={() => setActive(u, !u.active)}>{u.active ? "Disable" : "Enable"}</Button>
                            <Button size="sm" variant="danger" onClick={() => remove(u)}>Delete</Button>
                          </>
                        )}
                      </div>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </Card>
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
          <Button onClick={onClose}>Cancel</Button>
          <Button variant="primary" onClick={save}
                  disabled={saving || (!user && (username.trim().length < 3 || password.length < 8))}>
            {saving ? "Saving…" : "Save"}
          </Button>
        </>
      }
    >
      <div className="flex flex-col gap-4">
        {!user && (
          <Field label="Username" hint="Used to sign in. Letters, digits, '.', '_' and '-'; cannot be changed later.">
            <input value={username} onChange={(e) => setUsername(e.target.value)} autoFocus autoComplete="off" />
          </Field>
        )}
        <Field label="Full name">
          <input value={fullName} onChange={(e) => setFullName(e.target.value)} maxLength={80} autoFocus={!!user} />
        </Field>
        <div className="flex flex-col gap-1.5">
          <span className="text-[13px] font-medium">Role</span>
          {(Object.keys(ROLES) as Role[]).map((r) => (
            <label key={r} className={cn(
              "flex cursor-pointer items-start gap-2.5 rounded-lg border border-line-strong px-3 py-2.5 transition-colors",
              role === r && "border-accent bg-accent/5",
              isSelf && "cursor-default opacity-70",
            )}>
              <input type="radio" name="role" checked={role === r} disabled={isSelf} onChange={() => setRole(r)} className="mt-0.5" />
              <span className="flex flex-col gap-0.5">
                <strong className="font-medium">{ROLES[r].label}</strong>
                <span className="text-[12px] text-muted">{ROLES[r].description}</span>
              </span>
            </label>
          ))}
          {isSelf && <span className="text-[12px] text-muted">You cannot change your own role.</span>}
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
