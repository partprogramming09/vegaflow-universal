//! E2.7: which `config.json` numbers Configuración must mark "applies on restart".

use super::ffi_guard;
use crate::policy::config_file;

/// True when the number with this `VF_CONFIG_KEY_*` bit takes effect while VegaFlow runs. False for an unknown bit or a
/// panic: the cautious answer is to tell the person to restart.
#[no_mangle]
pub extern "C" fn vf_rust_config_number_applies_live(key_bit: u32) -> bool {
    ffi_guard(false, || config_file::number_applies_live(key_bit))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::policy::config_file::*;

    #[test]
    fn the_five_numbers_ask_for_a_restart_and_an_unknown_bit_does_too() {
        for bit in [
            VF_CONFIG_KEY_POLL_INTERVAL_MS,
            VF_CONFIG_KEY_MIN_FREE_RAM_MB,
            VF_CONFIG_KEY_MIN_STANDBY_MB,
            VF_CONFIG_KEY_COOLDOWN_SECONDS,
            VF_CONFIG_KEY_MAX_GAME_DISK_READ_MB_S,
            1 << 31,
            0,
        ] {
            assert!(!vf_rust_config_number_applies_live(bit), "{bit}");
        }
    }
}
