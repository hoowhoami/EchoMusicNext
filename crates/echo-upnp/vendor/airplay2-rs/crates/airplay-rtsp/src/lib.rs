//! # airplay-rtsp
//!
//! RTSP protocol implementation for AirPlay 2.
//!
//! This crate provides:
//! - RTSP client connection management
//! - Request/response formatting
//! - Binary plist payload handling
//! - Session management
//! - Two-phase SETUP handling

mod connection;
mod plist_codec;
pub mod raop_session;
mod request;
mod response;
pub mod sdp;
mod session;
mod traits;

pub use connection::{RtspConnection, SharedRtspConnection};
pub use plist_codec::{AudioFormatInfo, InfoResponse};
pub use raop_session::RaopSession;
pub use request::{RtspMethod, RtspRequest};
pub use response::RtspResponse;
pub use sdp::SdpBuilder;
pub use session::{RtspSession, SessionState};
pub use traits::RtspTransport;
