//! # airplay-timing
//!
//! PTP and NTP timing synchronization for AirPlay 2.
//!
//! This crate provides:
//! - NTP-like timing for AirPlay 1 devices
//! - PTP (IEEE 1588) timing for AirPlay 2 multi-room
//! - BMCA yield flow (Mac-style gPTP: negotiate, yield master to HomePod, sync as slave)
//! - Clock offset calculation
//! - RTP timestamp correlation

mod clock;
mod ntp;
mod ptp;
mod traits;

pub use clock::{
    ns_to_samples, ntp_to_unix, samples_to_ns, unix_to_ntp, Clock, ClockOffset, TimestampPair,
    NTP_EPOCH_OFFSET,
};
pub use ntp::{timing_sync_loop, NtpTimingClient, NtpTimingServer};
pub use ptp::{
    apple_subtype, ptp_sync_loop, AppleTlv, run_bmca_yield_flow, run_ptp_group_master_flow, run_ptp_slave, send_mac_style_signaling,
    send_ptp_announce, send_ptp_signaling, send_ptp_sync, send_stop_signaling, PtpClient,
    PtpMaster, PTP_EVENT_PORT, PTP_GENERAL_PORT,
};
pub use traits::TimingProtocol;
