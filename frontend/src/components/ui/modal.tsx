import type { ReactNode } from "react";
import * as Dialog from "@radix-ui/react-dialog";
import { X } from "@phosphor-icons/react";
import { cn } from "../../lib/cn";

/** Accessible dialog (focus trap, Esc, overlay click) with the app's chrome. */
export function Modal({ title, onClose, children, footer, wide }: {
  title: string; onClose: () => void; children: ReactNode; footer?: ReactNode; wide?: boolean;
}) {
  return (
    <Dialog.Root open onOpenChange={(open) => { if (!open) onClose(); }}>
      <Dialog.Portal>
        <Dialog.Overlay className="fixed inset-0 z-50 grid place-items-center bg-black/50 p-4 backdrop-blur-[2px] data-[state=open]:animate-[fade-in_.12s_ease-out]" />
        <Dialog.Content
          className={cn(
            "fixed left-1/2 top-1/2 z-50 flex max-h-[calc(100vh-32px)] w-full -translate-x-1/2 -translate-y-1/2 flex-col rounded-xl border border-line bg-surface shadow-[var(--shadow-pop)] focus:outline-none",
            wide ? "max-w-[920px]" : "max-w-[540px]",
          )}
        >
          <div className="flex items-center justify-between border-b border-line px-5 py-3.5">
            <Dialog.Title className="text-[15px] font-semibold">{title}</Dialog.Title>
            <Dialog.Close asChild>
              <button className="grid size-7 place-items-center rounded-md text-muted transition-colors hover:bg-surface-2 hover:text-text" aria-label="Close">
                <X size={16} weight="bold" />
              </button>
            </Dialog.Close>
          </div>
          <div className="overflow-y-auto px-5 py-4">{children}</div>
          {footer && (
            <div className="flex justify-end gap-2 rounded-b-xl border-t border-line bg-surface-2 px-5 py-3">{footer}</div>
          )}
        </Dialog.Content>
      </Dialog.Portal>
    </Dialog.Root>
  );
}
