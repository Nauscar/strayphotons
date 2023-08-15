use crate::game::{game_destroy, game_init, game_start};

use std::process;

#[derive(Default)]
pub struct StrayPhotons(u64);

#[cfg(target_os = "android")]
extern "Rust" {
    static APP: std::sync::OnceLock<android_activity::AndroidApp>;
}

impl StrayPhotons {
    pub fn new() -> Self {
        StrayPhotons::default()
    }

    pub unsafe fn start(mut self) {
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

    #[cfg(target_os = "android")]
    pub unsafe fn set_app(app: android_activity::AndroidApp) {
        APP.get_or_init(|| { app });
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
