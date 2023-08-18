extern crate sp_sys;

mod sp;
pub use sp::StrayPhotons;

#[cfg(target_os = "android")]
pub use android_activity::AndroidApp;

#[cfg(test)]
mod tests {
    use crate::sp::StrayPhotons;
    use std::error::Error;

    #[test]
    fn test_sp() -> Result<(), Box<dyn Error>> {
        let sp = StrayPhotons::new().start();
        Ok(())
    }
}
