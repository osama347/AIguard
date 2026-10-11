import type { ReactNode } from "react";
import * as RTooltip from "@radix-ui/react-tooltip";
import * as RSwitch from "@radix-ui/react-switch";
import * as RSelect from "@radix-ui/react-select";
import { Check, CaretDown, CaretUp } from "@phosphor-icons/react";
import { cn } from "../../lib/cn";

export function Tooltip({ label, children, side = "bottom" }: { label: string; children: ReactNode; side?: "top" | "bottom" | "left" | "right" }) {
  return (
    <RTooltip.Provider delayDuration={250}>
      <RTooltip.Root>
        <RTooltip.Trigger asChild>{children}</RTooltip.Trigger>
        <RTooltip.Portal>
          <RTooltip.Content
            side={side}
            sideOffset={6}
            className="z-50 rounded-md border border-line bg-surface px-2 py-1 text-[12px] text-text shadow-[var(--shadow-pop)]"
          >
            {label}
            <RTooltip.Arrow className="fill-[var(--surface)]" />
          </RTooltip.Content>
        </RTooltip.Portal>
      </RTooltip.Root>
    </RTooltip.Provider>
  );
}

export function Switch({ checked, onCheckedChange, id }: { checked: boolean; onCheckedChange: (v: boolean) => void; id?: string }) {
  return (
    <RSwitch.Root
      id={id}
      checked={checked}
      onCheckedChange={onCheckedChange}
      className={cn(
        "relative h-[22px] w-[38px] shrink-0 rounded-full border transition-colors",
        checked ? "border-transparent bg-ink" : "border-line-strong bg-surface-2",
      )}
    >
      <RSwitch.Thumb className={cn(
        "block size-[16px] translate-x-[3px] rounded-full bg-surface shadow-sm transition-transform",
        checked ? "translate-x-[19px]" : "bg-faint",
      )} />
    </RSwitch.Root>
  );
}

export interface SelectOption { value: string; label: string }

export function Select({ value, onValueChange, options, placeholder, className }: {
  value: string;
  onValueChange: (v: string) => void;
  options: SelectOption[];
  placeholder?: string;
  className?: string;
}) {
  return (
    <RSelect.Root value={value} onValueChange={onValueChange}>
      <RSelect.Trigger
        className={cn(
          "inline-flex h-9 min-w-[160px] items-center justify-between gap-2 rounded-md border border-line-strong bg-surface px-3 text-sm text-text transition-colors hover:bg-surface-2 data-[placeholder]:text-muted",
          className,
        )}
      >
        <RSelect.Value placeholder={placeholder} />
        <RSelect.Icon><CaretDown size={14} className="text-muted" /></RSelect.Icon>
      </RSelect.Trigger>
      <RSelect.Portal>
        <RSelect.Content
          position="popper"
          sideOffset={4}
          className="z-50 max-h-72 min-w-[var(--radix-select-trigger-width)] overflow-hidden rounded-lg border border-line bg-surface p-1 shadow-[var(--shadow-pop)]"
        >
          <RSelect.ScrollUpButton className="grid h-6 place-items-center text-muted"><CaretUp size={14} /></RSelect.ScrollUpButton>
          <RSelect.Viewport>
            {options.map((o) => (
              <RSelect.Item
                key={o.value}
                value={o.value}
                className="flex cursor-pointer select-none items-center justify-between gap-3 rounded-md px-2.5 py-1.5 text-[13px] text-text outline-none data-[highlighted]:bg-surface-2"
              >
                <RSelect.ItemText>{o.label}</RSelect.ItemText>
                <RSelect.ItemIndicator><Check size={14} weight="bold" /></RSelect.ItemIndicator>
              </RSelect.Item>
            ))}
          </RSelect.Viewport>
          <RSelect.ScrollDownButton className="grid h-6 place-items-center text-muted"><CaretDown size={14} /></RSelect.ScrollDownButton>
        </RSelect.Content>
      </RSelect.Portal>
    </RSelect.Root>
  );
}

export function Segmented<T extends string>({ value, onChange, options }: {
  value: T;
  onChange: (v: T) => void;
  options: { value: T; label: ReactNode }[];
}) {
  return (
    <div className="inline-flex shrink-0 gap-0.5 rounded-md border border-line-strong bg-surface-2 p-0.5" role="group">
      {options.map((o) => (
        <button
          key={o.value}
          type="button"
          onClick={() => onChange(o.value)}
          className={cn(
            "inline-flex items-center gap-1.5 rounded-[5px] px-2.5 py-1 text-[12px] font-medium transition-colors",
            value === o.value ? "bg-surface text-text shadow-[var(--shadow-xs)]" : "text-muted hover:text-text",
          )}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}
