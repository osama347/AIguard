import { useEffect, useMemo, useRef, useState } from "react";
import { useNavigate } from "react-router-dom";
import * as Dialog from "@radix-ui/react-dialog";
import { MagnifyingGlass, ArrowElbowDownLeft } from "@phosphor-icons/react";
import { cn } from "../lib/cn";
import { useTheme } from "../lib/theme";
import { useSession } from "../session";
import { NAV_ITEMS } from "./nav";

interface Command {
  id: string;
  label: string;
  group: string;
  icon?: React.ReactNode;
  run: () => void;
}

/** ⌘K / Ctrl-K quick switcher: jump to any screen or change the theme. */
export function CommandPalette({ open, onOpenChange }: { open: boolean; onOpenChange: (v: boolean) => void }) {
  const navigate = useNavigate();
  const { isAdmin, signOut } = useSession();
  const { theme, setTheme } = useTheme();
  const [query, setQuery] = useState("");
  const [active, setActive] = useState(0);
  const listRef = useRef<HTMLDivElement>(null);

  const commands = useMemo<Command[]>(() => {
    const nav: Command[] = NAV_ITEMS.filter((n) => !n.admin || isAdmin).map((n) => ({
      id: `go:${n.to}`,
      label: n.label,
      group: "Go to",
      icon: <n.icon size={16} />,
      run: () => navigate(n.to),
    }));
    const actions: Command[] = [
      { id: "theme:light", label: "Theme: Light", group: "Appearance", run: () => setTheme("light") },
      { id: "theme:dark", label: "Theme: Dark", group: "Appearance", run: () => setTheme("dark") },
      { id: "theme:system", label: "Theme: System", group: "Appearance", run: () => setTheme("system") },
    ];
    return [...nav, ...actions, { id: "signout", label: "Sign out", group: "Account", run: () => void signOut() }];
  }, [isAdmin, navigate, setTheme, signOut]);

  const filtered = useMemo(() => {
    const q = query.trim().toLowerCase();
    if (!q) return commands;
    return commands.filter((c) => c.label.toLowerCase().includes(q) || c.group.toLowerCase().includes(q));
  }, [commands, query]);

  useEffect(() => { setActive(0); }, [query, open]);
  useEffect(() => { if (!open) setQuery(""); }, [open]);
  useEffect(() => {
    listRef.current?.querySelector<HTMLElement>(`[data-index="${active}"]`)?.scrollIntoView({ block: "nearest" });
  }, [active]);

  const run = (c: Command) => { onOpenChange(false); c.run(); };

  const onKeyDown = (e: React.KeyboardEvent) => {
    if (e.key === "ArrowDown") { e.preventDefault(); setActive((a) => Math.min(a + 1, filtered.length - 1)); }
    else if (e.key === "ArrowUp") { e.preventDefault(); setActive((a) => Math.max(a - 1, 0)); }
    else if (e.key === "Enter" && filtered[active]) { e.preventDefault(); run(filtered[active]); }
  };

  let lastGroup = "";
  return (
    <Dialog.Root open={open} onOpenChange={onOpenChange}>
      <Dialog.Portal>
        <Dialog.Overlay className="fixed inset-0 z-[70] bg-black/50 backdrop-blur-[2px] data-[state=open]:animate-[fade-in_.1s_ease-out]" />
        <Dialog.Content
          aria-label="Command palette"
          onKeyDown={onKeyDown}
          className="fixed left-1/2 top-[14vh] z-[70] w-full max-w-[520px] -translate-x-1/2 overflow-hidden rounded-xl border border-line bg-surface shadow-[var(--shadow-pop)] focus:outline-none"
        >
          <Dialog.Title className="sr-only">Command palette</Dialog.Title>
          <div className="flex items-center gap-2.5 border-b border-line px-3.5">
            <MagnifyingGlass size={16} className="shrink-0 text-muted" />
            <input
              autoFocus
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              placeholder="Jump to a page or run a command…"
              className="h-11 w-full border-0 bg-transparent p-0 text-sm outline-none placeholder:text-faint focus:shadow-none"
            />
            <kbd className="shrink-0">Esc</kbd>
          </div>
          <div ref={listRef} className="max-h-[52vh] overflow-y-auto p-1.5">
            {!filtered.length && <div className="px-3 py-6 text-center text-[13px] text-muted">No matches</div>}
            {filtered.map((c, i) => {
              const header = c.group !== lastGroup ? c.group : null;
              lastGroup = c.group;
              return (
                <div key={c.id}>
                  {header && <div className="px-2.5 pb-1 pt-2.5 text-[10.5px] font-semibold uppercase tracking-wider text-faint">{header}</div>}
                  <button
                    data-index={i}
                    onMouseMove={() => setActive(i)}
                    onClick={() => run(c)}
                    className={cn(
                      "flex w-full items-center gap-2.5 rounded-md px-2.5 py-2 text-left text-[13px] transition-colors",
                      i === active ? "bg-surface-2 text-text" : "text-muted",
                    )}
                  >
                    <span className="grid size-4 place-items-center text-muted">{c.icon}</span>
                    <span className="flex-1 truncate">{c.label}</span>
                    {c.id.startsWith("theme:") && theme === c.id.slice(6) && <span className="text-[11px] text-muted">current</span>}
                    {i === active && <ArrowElbowDownLeft size={13} className="text-faint" />}
                  </button>
                </div>
              );
            })}
          </div>
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
