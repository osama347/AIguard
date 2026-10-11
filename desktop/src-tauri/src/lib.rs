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

#[derive(Serialize)]
struct UpdateCheckResult {
    available: bool,
    current_version: String,
    latest_version: Option<String>,
}

// Checks GitHub Releases for a newer signed build (the endpoint and public key
// are in tauri.conf.json) and, if the user agrees, installs it and restarts.
// Called silently at startup (failures ignored: no internet, or no release yet,
// must never get in the way of using the app on a LAN) and invokable from the
// frontend's "Check for updates" button, which does want to see the result.
#[tauri::command]
async fn check_for_update(app: tauri::AppHandle) -> Result<UpdateCheckResult, String> {
    use tauri_plugin_dialog::{DialogExt, MessageDialogButtons};
    use tauri_plugin_updater::UpdaterExt;

    let current_version = app.package_info().version.to_string();
    let updater = app.updater().map_err(|e| e.to_string())?;
    let Some(update) = updater.check().await.map_err(|e| e.to_string())? else {
        return Ok(UpdateCheckResult { available: false, current_version, latest_version: None });
    };
    let latest_version = update.version.clone();
    let ask = app
        .dialog()
        .message(format!(
            "Guard++ Desktop {} is available (you have {}). Install it now? The app will restart.",
            update.version, update.current_version
        ))
        .title("Update available")
        .buttons(MessageDialogButtons::OkCancelCustom("Install".into(), "Later".into()));
    let (tx, rx) = std::sync::mpsc::channel();
    ask.show(move |yes| {
        let _ = tx.send(yes);
    });
    let yes = tauri::async_runtime::spawn_blocking(move || rx.recv().unwrap_or(false))
        .await
        .unwrap_or(false);
    if yes {
        update
            .download_and_install(|_, _| {}, || {})
            .await
            .map_err(|e| e.to_string())?;
        app.restart();
    }
    Ok(UpdateCheckResult { available: true, current_version, latest_version: Some(latest_version) })
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_updater::Builder::new().build())
        .setup(|app| {
            let handle = app.handle().clone();
            tauri::async_runtime::spawn(async move {
                let _ = check_for_update(handle).await;
            });
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![discover_servers, check_for_update])
        .run(tauri::generate_context!())
        .expect("failed to start the Guard++ desktop app");
}
