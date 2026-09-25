import { HashRouter, Navigate, Route, Routes } from "react-router-dom";
import { SessionProvider, useSession } from "./session";
import { Layout } from "./components/Layout";
import { Spinner } from "./components/ui";
import { ConnectPage, LoginPage, SetupPage } from "./pages/Auth";
import { AnalyzePage } from "./pages/Analyze";
import { JobDetailPage } from "./pages/JobDetail";
import { AccessLogPage, AlertsPage, JobsPage } from "./pages/Records";
import { DriversPage, VehiclesPage } from "./pages/Fleet";
import { SettingsPage } from "./pages/Settings";
import { LivePage } from "./pages/Live";
import { CamerasPage } from "./pages/Cameras";
import { UsersPage } from "./pages/Users";
import { DashboardPage } from "./pages/Dashboard";
import { getToken } from "./api/client";
import type { ReactNode } from "react";

/** Landing page: admins start on the dashboard, guards on the live view. */
function Home() {
  const { isAdmin } = useSession();
  return <Navigate to={isAdmin ? "/dashboard" : "/live"} replace />;
}

/** Pages only administrators may open; guards are sent to the live view. */
function AdminOnly({ children }: { children: ReactNode }) {
  const { isAdmin } = useSession();
  return isAdmin ? <>{children}</> : <Navigate to="/live" replace />;
}

function Gate() {
  const { health, healthError, user } = useSession();
  if (!health && healthError) return <ConnectPage />;
  if (!health) return <div className="center"><Spinner label="Connecting…" /></div>;
  if (health.setup_required) return <SetupPage />;
  if (!getToken()) return <LoginPage />;
  if (!user) return <div className="center"><Spinner label="Signing in…" /></div>;

  return (
    <Routes>
      <Route element={<Layout />}>
        <Route path="/dashboard" element={<AdminOnly><DashboardPage /></AdminOnly>} />
        <Route path="/live" element={<LivePage />} />
        <Route path="/cameras" element={<AdminOnly><CamerasPage /></AdminOnly>} />
        <Route path="/analyze" element={<AdminOnly><AnalyzePage /></AdminOnly>} />
        <Route path="/jobs" element={<AdminOnly><JobsPage /></AdminOnly>} />
        <Route path="/jobs/:id" element={<AdminOnly><JobDetailPage /></AdminOnly>} />
        <Route path="/access" element={<AccessLogPage />} />
        <Route path="/alerts" element={<AlertsPage />} />
        <Route path="/drivers" element={<DriversPage />} />
        <Route path="/vehicles" element={<VehiclesPage />} />
        <Route path="/users" element={<AdminOnly><UsersPage /></AdminOnly>} />
        <Route path="/settings" element={<SettingsPage />} />
        <Route path="*" element={<Home />} />
      </Route>
    </Routes>
  );
}

// Hash routing: works identically when served by guard-core and inside the desktop app.
export default function App() {
  return (
    <SessionProvider>
      <HashRouter>
        <Gate />
      </HashRouter>
    </SessionProvider>
  );
}
