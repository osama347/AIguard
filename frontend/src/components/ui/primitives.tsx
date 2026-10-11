import type { ReactNode, ButtonHTMLAttributes } from "react";
import { WarningCircle } from "@phosphor-icons/react";
import { assetUrl, type Community } from "../../api/client";
import { cn } from "../../lib/cn";
import type { Tone } from "../../lib/format";

/* ------------------------------------------------------------------ tone maps */

const TONE_CHIP: Record<Tone, string> = {
  ok: "text-ok bg-ok/10 border-ok/20",
  bad: "text-bad bg-bad/10 border-bad/20",
  warn: "text-warn bg-warn/12 border-warn/20",
  info: "text-info bg-info/10 border-info/20",
  muted: "text-muted bg-surface-2 border-line",
};

const TONE_DOT: Record<Tone, string> = {
  ok: "bg-ok",
  bad: "bg-bad",
  warn: "bg-warn",
  info: "bg-info",
  muted: "bg-faint",
};

/** Subtle status chip: tinted surface, colored text, leading dot. */
export function Badge({ tone = "muted", children, dot = true }: { tone?: Tone; children: ReactNode; dot?: boolean }) {
  return (
    <span className={cn(
      "inline-flex items-center gap-1.5 whitespace-nowrap rounded-md border px-1.5 py-0.5 text-[11px] font-medium leading-5",
      TONE_CHIP[tone],
    )}>
      {dot && <span className={cn("size-1.5 shrink-0 rounded-full", TONE_DOT[tone])} aria-hidden />}
      {children}
    </span>
  );
}

export function StatusDot({ tone = "muted", className }: { tone?: Tone; className?: string }) {
  return <span className={cn("inline-block size-2 shrink-0 rounded-full", TONE_DOT[tone], className)} aria-hidden />;
}

/* ------------------------------------------------------------------ brand */

export function Brand({ community, size }: { community: Community | null; size: number }) {
  const named = !!community?.configured;
  return (
    <span className="flex min-w-0 items-center gap-2.5">
      <img
        src={community?.logo_url ? assetUrl(community.logo_url) : "./favicon.svg"}
        alt=""
        width={size}
        height={size}
        className="shrink-0 object-contain"
      />
      <span className="flex min-w-0 flex-col leading-tight">
        <span className="truncate font-semibold tracking-tight">{named ? community!.name : "Guard++"}</span>
        {named && <span className="truncate text-[11px] font-normal text-muted">Guard++</span>}
      </span>
    </span>
  );
}

/* ------------------------------------------------------------------ buttons */

type Variant = "primary" | "secondary" | "ghost" | "danger";
type Size = "sm" | "md";

const VARIANT: Record<Variant, string> = {
  primary: "border-transparent bg-ink text-ink-text hover:opacity-90",
  secondary: "border-line-strong bg-surface text-text hover:bg-surface-2",
  ghost: "border-transparent bg-transparent text-muted hover:bg-surface-2 hover:text-text",
  danger: "border-line-strong bg-transparent text-bad hover:bg-bad/10",
};

const SIZE: Record<Size, string> = {
  sm: "h-7 gap-1.5 px-2.5 text-[12.5px]",
  md: "h-9 gap-2 px-3.5 text-sm",
};

export function buttonClass(variant: Variant = "secondary", size: Size = "md", extra?: string) {
  return cn(
    "inline-flex select-none items-center justify-center whitespace-nowrap rounded-md border font-medium transition-[background-color,border-color,opacity] disabled:pointer-events-none disabled:opacity-50",
    VARIANT[variant],
    SIZE[size],
    extra,
  );
}

export interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: Variant;
  size?: Size;
}

export function Button({ variant = "secondary", size = "md", className, ...rest }: ButtonProps) {
  return <button className={buttonClass(variant, size, className)} {...rest} />;
}

export function IconButton({ className, ...rest }: ButtonHTMLAttributes<HTMLButtonElement>) {
  return (
    <button
      className={cn(
        "inline-grid size-8 place-items-center rounded-md border border-transparent text-muted transition-colors hover:bg-surface-2 hover:text-text disabled:opacity-50",
        className,
      )}
      {...rest}
    />
  );
}

/* ------------------------------------------------------------------ states */

export function Spinner({ label }: { label?: string }) {
  return (
    <span className="inline-flex items-center gap-2 text-muted" role="status">
      <span className="size-[15px] animate-[spin_.7s_linear_infinite] rounded-full border-2 border-line-strong border-t-muted" aria-hidden />
      {label && <span>{label}</span>}
    </span>
  );
}

export function ErrorBox({ error, onRetry }: { error: Error | string | null | undefined; onRetry?: () => void }) {
  if (!error) return null;
  return (
    <div
      className="my-2 flex items-center justify-between gap-3 rounded-md border border-bad/25 bg-bad/10 px-3 py-2.5 text-[13px] text-bad"
      role="alert"
    >
      <span className="flex items-center gap-2">
        <WarningCircle size={16} weight="fill" className="shrink-0" />
        {typeof error === "string" ? error : error.message}
      </span>
      {onRetry && <Button size="sm" variant="secondary" className="text-text" onClick={onRetry}>Retry</Button>}
    </div>
  );
}

export function Empty({ title, children }: { title: string; children?: ReactNode }) {
  return (
    <div className="mb-4 flex flex-col items-center gap-1.5 rounded-lg border border-dashed border-line-strong px-6 py-12 text-center">
      <strong className="font-medium text-text">{title}</strong>
      {children && <div className="max-w-md text-[13px] text-muted">{children}</div>}
    </div>
  );
}

/* ------------------------------------------------------------------ page header */

export function PageHeader({ title, subtitle, actions }: { title: string; subtitle?: ReactNode; actions?: ReactNode }) {
  return (
    <div className="mb-5 flex flex-wrap items-start justify-between gap-4">
      <div className="min-w-0">
        <h1 className="text-[22px] font-semibold tracking-tight">{title}</h1>
        {subtitle && <p className="mt-1 max-w-[72ch] text-[13px] text-muted">{subtitle}</p>}
      </div>
      {actions && <div className="flex items-center gap-2">{actions}</div>}
    </div>
  );
}

/* ------------------------------------------------------------------ form field */

export function Field({ label, hint, children }: { label: string; hint?: ReactNode; children: ReactNode }) {
  return (
    <label className="flex min-w-0 flex-1 flex-col gap-1.5">
      <span className="text-[13px] font-medium">{label}</span>
      {children}
      {hint && <span className="text-[12px] text-muted">{hint}</span>}
    </label>
  );
}

/* ------------------------------------------------------------------ form section */

/** A titled group of fields, so long forms read as steps instead of a wall of inputs.
 *  Use one per logical part of a form (Details / Face photos / Authorized vehicles…). */
export function FormSection({ title, hint, children }: { title: string; hint?: ReactNode; children: ReactNode }) {
  return (
    <section className="flex flex-col gap-3">
      <div>
        <h3 className="text-[11px] font-semibold uppercase tracking-[0.06em] text-faint">{title}</h3>
        {hint && <p className="mt-0.5 text-[12px] text-muted">{hint}</p>}
      </div>
      <div className="flex flex-col gap-3">{children}</div>
    </section>
  );
}

/* ------------------------------------------------------------------ progress */

export function Progress({ value, tone = "info" }: { value: number; tone?: Tone }) {
  const clamped = Math.max(0, Math.min(1, value));
  return (
    <div
      className="h-1.5 overflow-hidden rounded-full bg-surface-3"
      role="progressbar"
      aria-valuenow={Math.round(clamped * 100)}
      aria-valuemin={0}
      aria-valuemax={100}
    >
      <div className={cn("h-full rounded-full transition-[width]", tone === "ok" ? "bg-ok" : "bg-info")} style={{ width: `${clamped * 100}%` }} />
    </div>
  );
}

/* ------------------------------------------------------------------ card */

export function Card({ flush, className, children }: { flush?: boolean; className?: string; children: ReactNode }) {
  return (
    <section className={cn("mb-3.5 min-w-0 rounded-lg border border-line bg-surface", flush ? "overflow-hidden p-0" : "px-4 py-4", className)}>
      {children}
    </section>
  );
}

export function CardHead({ title, sub, actions }: { title: ReactNode; sub?: ReactNode; actions?: ReactNode }) {
  return (
    <div className="mb-3 flex items-center justify-between gap-3">
      <div className="min-w-0">
        <h2 className="text-sm font-semibold">{title}</h2>
        {sub && <div className="text-[12px] text-muted">{sub}</div>}
      </div>
      {actions}
    </div>
  );
}

/* ------------------------------------------------------------------ notice */

const NOTICE_TONE = {
  warn: "border-warn/25 bg-warn/12",
  ok: "border-ok/25 bg-ok/10",
  info: "border-info/25 bg-info/10",
} as const;

export function Notice({ tone = "warn", children }: { tone?: keyof typeof NOTICE_TONE; children: ReactNode }) {
  return <div className={cn("mb-3.5 rounded-lg border px-3.5 py-2.5 text-[13px]", NOTICE_TONE[tone])}>{children}</div>;
}

/* ------------------------------------------------------------------ facts */

export function Facts({ children }: { children: ReactNode }) {
  return <dl className="m-0 grid grid-cols-[max-content_minmax(0,1fr)] items-baseline gap-x-5 gap-y-2">{children}</dl>;
}

export function Fact({ label, children }: { label: ReactNode; children: ReactNode }) {
  return (
    <>
      <dt className="text-muted">{label}</dt>
      <dd className="m-0 min-w-0 break-words">{children}</dd>
    </>
  );
}

/* ------------------------------------------------------------------ chip */

export function Chip({ tone = "muted", children }: { tone?: "ok" | "bad" | "muted"; children: ReactNode }) {
  return (
    <span
      className={cn(
        "rounded-md border bg-surface-2 px-2 py-0.5 font-mono text-[12px]",
        tone === "ok" ? "border-ok/40 text-ok" : tone === "bad" ? "border-bad/40 text-bad" : "border-line text-text",
      )}
    >
      {children}
    </span>
  );
}

/* ------------------------------------------------------------------ avatar */

export function Avatar({ url, alt, size = 32 }: { url?: string | null; alt: string; size?: number }) {
  return (
    <img
      src={url ?? undefined}
      alt={alt}
      width={size}
      height={size}
      className="shrink-0 rounded-lg border border-line bg-surface-2 object-cover"
    />
  );
}
