import {
  SquaresFour, VideoCamera, ListBullets, Bell, UsersThree,
  Camera, UserGear, FilmStrip, ListChecks, GearSix,
  type Icon,
} from "@phosphor-icons/react";

export interface NavItem {
  to: string;
  label: string;
  section?: string;
  admin?: boolean;
  icon: Icon;
  badge?: "alerts";
  /** false keeps it out of the sidebar while still searchable in the command palette —
   *  for account/app config (e.g. Settings) that already lives in the avatar menu and
   *  doesn't belong among the sidebar's task areas. */
  sidebar?: boolean;
}

/** Single source of truth for navigation, shared by the sidebar and the command palette. */
export const NAV_ITEMS: NavItem[] = [
  { to: "/dashboard", label: "Dashboard", admin: true, icon: SquaresFour },
  { to: "/live", label: "Live", icon: VideoCamera },
  { to: "/access", label: "Access log", icon: ListBullets },
  { to: "/alerts", label: "Alerts", icon: Bell, badge: "alerts" },

  { to: "/fleet", label: "Fleet", section: "Manage", icon: UsersThree },
  { to: "/cameras", label: "Cameras", admin: true, icon: Camera },
  { to: "/users", label: "Users", admin: true, icon: UserGear },

  { to: "/analyze", label: "Test a video", section: "Testing", admin: true, icon: FilmStrip },
  { to: "/jobs", label: "Test results", admin: true, icon: ListChecks },

  { to: "/settings", label: "Settings", icon: GearSix, sidebar: false },
];
