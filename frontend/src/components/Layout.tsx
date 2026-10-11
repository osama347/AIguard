import { Fragment, useEffect, useState } from "react";
import { NavLink, Outlet, useNavigate } from "react-router-dom";
import * as DropdownMenu from "@radix-ui/react-dropdown-menu";
import * as Dialog from "@radix-ui/react-dialog";
import {
  CaretDown, GearSix, SignOut, Sun, Moon, Monitor, Bell, List, Check,
} from "@phosphor-icons/react";
import { useSession } from "../session";
import { cn } from "../lib/cn";
import { useTheme, type Theme } from "../lib/theme";
import { notificationsEnabled, requestNotifications } from "../lib/notify";
import { Brand, Button, Badge } from "./ui";
import { NAV_ITEMS, type NavItem } from "./nav";
import { ConnectionStatus } from "./ConnectionStatus";
import { CommandPalette } from "./CommandPalette";

function NavItems({ onNavigate }: { onNavigate?: () => void }) {
  const { isAdmin, openAlerts } = useSession();
  return (
    <nav className="flex flex-1 flex-col gap-0.5 overflow-y-auto">
      {NAV_ITEMS.filter((n) => !n.admin || isAdmin).map((n, i) => {
        const prev: NavItem | undefined = NAV_ITEMS.filter((x) => !x.admin || isAdmin)[i - 1];
        const showSection = n.section !== undefined && n.section !== (prev?.section ?? "");
        return (
          <Fragment key={n.to}>
            {showSection && n.section && (
              <div className="px-2.5 pb-1 pt-5 text-[10.5px] font-semibold uppercase tracking-[0.06em] text-faint">{n.section}</div>
            )}
            {showSection && n.section === "" && <div className="h-4" />}
            <NavLink
              to={n.to}
              onClick={onNavigate}
              className={({ isActive }) =>
                cn(
                  "group flex items-center gap-2.5 rounded-md px-2.5 py-[7px] text-[13.5px] font-medium transition-colors",
                  isActive ? "bg-surface-2 text-text" : "text-muted hover:bg-surface-2/70 hover:text-text",
                )
              }
            >
              <n.icon size={16} className="shrink-0 opacity-90" />
              <span className="flex-1 truncate">{n.label}</span>
              {n.badge === "alerts" && openAlerts > 0 && (
                <span className="rounded-[5px] bg-bad/12 px-1.5 text-[11px] font-semibold text-bad">{openAlerts}</span>
              )}
            </NavLink>
          </Fragment>
        );
      })}
    </nav>
  );
}

function UserMenu() {
  const { user, isAdmin, signOut } = useSession();
  const { theme, setTheme } = useTheme();
  const navigate = useNavigate();
  const [notif, setNotif] = useState(true);
  useEffect(() => { notificationsEnabled().then(setNotif); }, []);

  const name = user?.full_name || user?.username || "";
  const initials = (user?.full_name || user?.username || "?").trim().slice(0, 1).toUpperCase();

  const item = "flex cursor-pointer select-none items-center gap-2.5 rounded-md px-2.5 py-1.5 text-[13px] text-text outline-none data-[highlighted]:bg-surface-2";
  const radio = "flex cursor-pointer select-none items-center gap-2.5 rounded-md px-2.5 py-1.5 text-[13px] text-text outline-none data-[highlighted]:bg-surface-2";

  return (
    <DropdownMenu.Root>
      <DropdownMenu.Trigger asChild>
        <button className="flex items-center gap-2 rounded-md border border-transparent py-1 pl-1 pr-2 text-left transition-colors hover:bg-surface-2">
          <span className="grid size-7 shrink-0 place-items-center rounded-md bg-surface-2 text-[12px] font-semibold text-muted">{initials}</span>
          <span className="hidden min-w-0 flex-col leading-tight sm:flex">
            <span className="max-w-[140px] truncate text-[13px] font-medium">{name}</span>
            <span className="text-[11px] text-muted">{isAdmin ? "Administrator" : "Guard"}</span>
          </span>
          <CaretDown size={12} className="text-muted" />
        </button>
      </DropdownMenu.Trigger>
      <DropdownMenu.Portal>
        <DropdownMenu.Content
          align="end"
          sideOffset={6}
          className="z-[60] w-60 rounded-lg border border-line bg-surface p-1 shadow-[var(--shadow-pop)]"
        >
          <DropdownMenu.Label className="px-2.5 py-1.5 text-[12px] text-muted">
            Signed in as <span className="font-medium text-text">{name}</span>
          </DropdownMenu.Label>
          <DropdownMenu.Separator className="my-1 h-px bg-line" />
          <DropdownMenu.Item className={item} onSelect={() => navigate("/settings")}>
            <GearSix size={15} className="text-muted" /> Settings
          </DropdownMenu.Item>
          {!notif && (
            <DropdownMenu.Item className={item} onSelect={() => requestNotifications().then(setNotif)}>
              <Bell size={15} className="text-muted" /> Enable alert notifications
            </DropdownMenu.Item>
          )}
          <DropdownMenu.Separator className="my-1 h-px bg-line" />
          <DropdownMenu.Label className="px-2.5 py-1 text-[10.5px] font-semibold uppercase tracking-wider text-faint">Theme</DropdownMenu.Label>
          <DropdownMenu.RadioGroup value={theme} onValueChange={(v) => setTheme(v as Theme)}>
            <DropdownMenu.RadioItem value="system" className={radio}>
              <Monitor size={15} className="text-muted" /> System
              <DropdownMenu.ItemIndicator className="ml-auto"><Check size={13} weight="bold" /></DropdownMenu.ItemIndicator>
            </DropdownMenu.RadioItem>
            <DropdownMenu.RadioItem value="light" className={radio}>
              <Sun size={15} className="text-muted" /> Light
              <DropdownMenu.ItemIndicator className="ml-auto"><Check size={13} weight="bold" /></DropdownMenu.ItemIndicator>
            </DropdownMenu.RadioItem>
            <DropdownMenu.RadioItem value="dark" className={radio}>
              <Moon size={15} className="text-muted" /> Dark
              <DropdownMenu.ItemIndicator className="ml-auto"><Check size={13} weight="bold" /></DropdownMenu.ItemIndicator>
            </DropdownMenu.RadioItem>
          </DropdownMenu.RadioGroup>
          <DropdownMenu.Separator className="my-1 h-px bg-line" />
          <DropdownMenu.Item className={cn(item, "text-bad data-[highlighted]:bg-bad/10")} onSelect={() => void signOut()}>
            <SignOut size={15} /> Sign out
          </DropdownMenu.Item>
        </DropdownMenu.Content>
      </DropdownMenu.Portal>
    </DropdownMenu.Root>
  );
}

function MobileNav({ open, onOpenChange }: { open: boolean; onOpenChange: (v: boolean) => void }) {
  const { community } = useSession();
  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <Dialog.Overlay className="fixed inset-0 z-[60] bg-black/50 backdrop-blur-[2px]" />
        <Dialog.Content className="fixed left-0 top-0 z-[60] flex h-full w-[260px] flex-col border-r border-line bg-surface p-3 focus:outline-none">
          <Dialog.Title className="sr-only">Navigation</Dialog.Title>
          <div className="px-2 pb-3 pt-1"><Brand community={community} size={24} /></div>
          <NavItems onNavigate={() => onOpenChange(false)} />
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
}

export function Layout() {
  const { community, toasts, dismissToast } = useSession();
  const navigate = useNavigate();
  const [palette, setPalette] = useState(false);
  const [mobileNav, setMobileNav] = useState(false);

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === "k") { e.preventDefault(); setPalette((p) => !p); }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  return (
    <div className="flex min-h-full">
      <aside className="sticky top-0 hidden h-screen w-[236px] shrink-0 flex-col border-r border-line bg-surface px-2.5 py-3.5 md:flex">
        <div className="px-2 pb-2 pt-1"><Brand community={community} size={26} /></div>
        <NavItems />
      </aside>

      <div className="flex min-w-0 flex-1 flex-col">
        <header className="sticky top-0 z-20 flex items-center gap-3 border-b border-line bg-surface/85 px-4 py-2.5 backdrop-blur sm:px-6">
          <button
            className="grid size-8 place-items-center rounded-md text-muted hover:bg-surface-2 hover:text-text md:hidden"
            onClick={() => setMobileNav(true)}
            aria-label="Open navigation"
          >
            <List size={18} />
          </button>
          <button
            onClick={() => setPalette(true)}
            className="flex h-8 max-w-[320px] flex-1 items-center gap-2 rounded-md border border-line-strong bg-surface-2/60 px-2.5 text-[13px] text-muted transition-colors hover:border-line-strong hover:bg-surface-2"
          >
            <span className="flex-1 text-left">Jump to…</span>
            <kbd>⌘K</kbd>
          </button>
          <div className="flex-1" />
          <ConnectionStatus />
          <UserMenu />
        </header>

        <main className="mx-auto w-full max-w-[1320px] px-4 py-6 pb-16 sm:px-6">
          <Outlet />
        </main>
      </div>

      <div className="fixed bottom-4 right-4 z-[60] flex max-w-[min(400px,calc(100vw-32px))] flex-col gap-2" aria-live="polite">
        {toasts.map((a) => (
          <div
            key={a.id}
            className={cn(
              "flex items-start justify-between gap-3 rounded-lg border border-line border-l-[3px] bg-surface p-3.5 shadow-[var(--shadow-pop)] animate-[toast-in_.18s_ease-out]",
              a.severity === "critical" ? "border-l-bad" : "border-l-warn",
            )}
          >
            <div className="min-w-0">
              <div className="mb-0.5 flex items-center gap-2">
                <Badge tone={a.severity === "critical" ? "bad" : "warn"}>{a.severity === "critical" ? "Critical" : "Alert"}</Badge>
              </div>
              <div className="text-[13px]">{a.message}</div>
            </div>
            <div className="flex shrink-0 items-center gap-1">
              <Button size="sm" onClick={() => { dismissToast(a.id); navigate("/alerts"); }}>View</Button>
              <button className="grid size-7 place-items-center rounded-md text-muted hover:bg-surface-2 hover:text-text" onClick={() => dismissToast(a.id)} aria-label="Dismiss">
                <span aria-hidden className="text-[15px] leading-none">×</span>
              </button>
            </div>
          </div>
        ))}
      </div>

      <CommandPalette open={palette} onOpenChange={setPalette} />
      <MobileNav open={mobileNav} onOpenChange={setMobileNav} />
    </div>
  );
}
