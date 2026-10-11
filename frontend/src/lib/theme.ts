import { useSyncExternalStore } from "react";

export type Theme = "system" | "light" | "dark";
export type ResolvedTheme = "light" | "dark";

const KEY = "guard.theme";
const listeners = new Set<() => void>();
const media = typeof window !== "undefined" ? window.matchMedia("(prefers-color-scheme: dark)") : null;

function readStored(): Theme {
  try {
    const v = localStorage.getItem(KEY);
    return v === "light" || v === "dark" || v === "system" ? v : "system";
  } catch {
    return "system";
  }
}

let theme: Theme = readStored();

export function getTheme(): Theme {
  return theme;
}

export function resolvedTheme(): ResolvedTheme {
  if (theme === "system") return media?.matches ? "dark" : "light";
  return theme;
}

function apply() {
  if (typeof document === "undefined") return;
  const dark = resolvedTheme() === "dark";
  document.documentElement.classList.toggle("dark", dark);
  document.documentElement.style.colorScheme = dark ? "dark" : "light";
}

export function setTheme(next: Theme) {
  theme = next;
  try {
    if (next === "system") localStorage.removeItem(KEY);
    else localStorage.setItem(KEY, next);
  } catch {
    /* storage unavailable: applies for this session only */
  }
  apply();
  listeners.forEach((fn) => fn());
}

export function initTheme() {
  apply();
  media?.addEventListener("change", () => {
    if (theme === "system") {
      apply();
      listeners.forEach((fn) => fn());
    }
  });
}

function subscribe(fn: () => void) {
  listeners.add(fn);
  return () => { listeners.delete(fn); };
}

/** React hook: current preference, the resolved light/dark value, and a setter. */
export function useTheme() {
  const current = useSyncExternalStore(subscribe, getTheme, () => theme);
  return { theme: current, resolved: resolvedTheme(), setTheme };
}
