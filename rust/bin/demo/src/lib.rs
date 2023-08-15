extern crate sp_sys;

use sp_sys::StrayPhotons;
use std::env;
use std::error::Error;

#[cfg(target_os = "android")]
fn init_logging() -> Result<(), Box<dyn Error>> {
    android_logger::init_once(
        android_logger::Config::default()
            .with_max_level(log::LevelFilter::Trace)
            .with_tag("demo"),
    );

    Ok(())
}

#[cfg(not(target_os = "android"))]
fn init_logging() -> Result<(), Box<dyn Error>> {
    simple_logger::SimpleLogger::new().init()?;
    Ok(())
}

fn start() -> Result<(), Box<dyn Error>> {
    init_logging()?;
    log::warn!("start");

    let sp = StrayPhotons::new();

    let current_dir = env::current_dir()?;
    log::info!("Rust working directory: {}", current_dir.display());

    log::warn!("init");

    // TODO: API for setting scripts/assets
    unsafe {
        sp.start();
    }

    Ok(())
}

fn start_app() {
    match start() {
        Ok(_) => {}
        Err(err) => {
            eprintln!("Error: {}", err);
        }
    }
}

#[cfg(target_os = "android")]
#[no_mangle]
fn android_main(app: android_activity::AndroidApp) {
    unsafe { StrayPhotons::set_app(app); }
    start_app();
}
