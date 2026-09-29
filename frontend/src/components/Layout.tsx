import { Fragment } from "react";
import { NavLink, Outlet, useNavigate } from "react-router-dom";
import { useSession } from "../session";
import { Brand } from "./ui";
import { ConnectionStatus } from "./ConnectionStatus";

// Guards see monitoring pages only; admins also manage the system.
const NAV: { to: string; label: string; section?: string; admin?: boolean }[] = [
  { to: "/dashboard", label: "Dashboard", admin: true },
  { to: "/live", label: "Live" },
  { to: "/access", label: "Access log" },
  { to: "/alerts", label: "Alerts" },
  { to: "/fleet", label: "Fleet" },
  { to: "/cameras", label: "Cameras", section: "Administration", admin: true },
  { to: "/users", label: "Users", admin: true },
  { to: "/analyze", label: "Test a video", section: "Testing", admin: true },
  { to: "/jobs", label: "Test results", admin: true },
  { to: "/settings", label: "Settings", section: "" },
];

export function Layout() {
  const { user, isAdmin, community, openAlerts, toasts, dismissToast } = useSession();
  const navigate = useNavigate();
  return (
    <div className="shell">
      <aside className="sidebar">
        <div className="brand">
          <Brand community={community} size={26} />
        </div>
        <nav>
          {NAV.filter((n) => !n.admin || isAdmin).map((n) => (
            <Fragment key={n.to}>
              {n.section !== undefined && (n.section ? <div className="nav-section">{n.section}</div> : <div className="nav-gap" />)}
              <NavLink to={n.to} className={({ isActive }) => `nav-link ${isActive ? "active" : ""}`}>
                <span>{n.label}</span>
                {n.to === "/alerts" && openAlerts > 0 && <span className="nav-count">{openAlerts}</span>}
              </NavLink>
            </Fragment>
          ))}
        </nav>
        <div className="sidebar-foot">
          <div className="break">{user?.full_name || user?.username}</div>
          <div className="muted small">{isAdmin ? "Administrator" : "Guard"}</div>
        </div>
      </aside>
      <div className="main">
        <header className="topbar">
          <ConnectionStatus />
        </header>
        <main className="content">
          <Outlet />
        </main>
      </div>
      <div className="toasts" aria-live="polite">
        {toasts.map((a) => (
          <div key={a.id} className={`toast toast-${a.severity}`}>
            <div>
              <strong>{a.severity === "critical" ? "Critical alert" : "Alert"}</strong>
              <div>{a.message}</div>
            </div>
            <div className="toast-actions">
              <button className="btn btn-small" onClick={() => { dismissToast(a.id); navigate("/alerts"); }}>View</button>
              <button className="icon-btn" onClick={() => dismissToast(a.id)} aria-label="Dismiss">×</button>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
