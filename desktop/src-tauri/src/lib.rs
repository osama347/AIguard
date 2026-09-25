// Guard++ desktop app.
//
// Deliberately thin: it hosts the same frontend that guard-core serves to
// browsers, and talks to a Guard++ server (the Jetson) over the LAN through the
// guard-core HTTP API. The shell holds no credentials and makes no API calls
// itself. It adds native OS notifications for alerts and LAN discovery of
// servers (mDNS service `_guard._tcp`, announced by the guard-server package).
use mdns_sd::{ServiceDaemon, ServiceEvent};
use serde::Serialize;
use std::time::{Duration, Instant};

#[derive(Serialize)]
struct FoundServer {
    name: String,
    url: String,
}

fn browse(timeout: Duration) -> Result<Vec<FoundServer>, String> {
    let daemon = ServiceDaemon::new().map_err(|e| e.to_string())?;
    let rx = daemon.browse("_guard._tcp.local.").map_err(|e| e.to_string())?;
    let deadline = Instant::now() + timeout;
    let mut found: Vec<FoundServer> = Vec::new();
    while let Some(left) = deadline.checked_duration_since(Instant::now()) {
        match rx.recv_timeout(left) {
            Ok(ServiceEvent::ServiceResolved(info)) => {
                // Prefer an IPv4 address: it works without scope ids on every OS.
                let addr = info
                    .get_addresses_v4()
                    .into_iter()
                    .next()
                    .map(|a| a.to_string());
                if let Some(addr) = addr {
                    let url = format!("http://{}:{}", addr, info.get_port());
                    if !found.iter().any(|f| f.url == url) {
                        let name = info.get_hostname().trim_end_matches('.').to_string();
                        found.push(FoundServer { name, url });
                    }
                }
            }
            Ok(_) => {}
            Err(_) => break,
        }
    }
    let _ = daemon.shutdown();
    Ok(found)
}

#[tauri::command]
async fn discover_servers() -> Result<Vec<FoundServer>, String> {
    tauri::async_runtime::spawn_blocking(|| browse(Duration::from_secs(3)))
        .await
        .map_err(|e| e.to_string())?
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_notification::init())
        .invoke_handler(tauri::generate_handler![discover_servers])
        .run(tauri::generate_context!())
        .expect("failed to start the Guard++ desktop app");
}
