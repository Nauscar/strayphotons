use sp_sys::{game_destroy, game_init, game_start};

use std::{env, error::Error, process};

#[cfg(target_os = "android")]
extern "Rust" {
    static APP: std::sync::OnceLock<crate::AndroidApp>;
}

#[derive(Default)]
pub struct StrayPhotons(u64);

impl StrayPhotons {
    #[cfg(not(target_os = "android"))]
    pub fn new() -> Self {
        StrayPhotons::default()
    }

    #[cfg(target_os = "android")]
    pub fn new(app: crate::AndroidApp) -> Self {
        unsafe {
            set_app(app);
        };
        StrayPhotons::default()
    }

    unsafe fn start(mut self) {
        let input = ["sp-rs\0"]; // , "--map\0", "sponza\0"
        let mut input_arr = input.map(|str| str.as_ptr());

        self.0 = game_init(input.len() as i32, input_arr.as_mut_ptr() as *mut *mut u8);
        let status = game_start(self.0);
        if status != 0 {
            eprintln!("Stray Photons exited with code: {}", status);
            // process::exit skips Drop, manually shutdown first.
            game_destroy(self.0);
            self.0 = 0;
            process::exit(status);
        }
    }

    fn _start(self) -> Result<(), Box<dyn Error>> {
        init_logging()?;
        log::warn!("start");

        let current_dir = env::current_dir()?;
        log::info!("Rust working directory: {}", current_dir.display());

        log::warn!("init");

        // TODO: API for setting scripts/assets
        unsafe {
            self.start();
        }

        Ok(())
    }

    pub fn start_app(self) {
        match self._start() {
            Ok(_) => {}
            Err(err) => {
                eprintln!("Error: {}", err);
            }
        }
    }
}

impl Drop for StrayPhotons {
    fn drop(&mut self) {
        if self.0 != 0 {
            unsafe {
                game_destroy(self.0);
            }
        }
    }
}

#[cfg(target_os = "android")]
unsafe fn set_app(app: android_activity::AndroidApp) {
    APP.get_or_init(|| app);
}

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
