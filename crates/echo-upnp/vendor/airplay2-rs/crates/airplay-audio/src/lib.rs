//! # airplay-audio
//!
//! Audio encoding and RTP streaming for AirPlay 2.
//!
//! This crate provides:
//! - Audio decoding from various formats (via symphonia)
//! - Live audio streaming from external sources (e.g., Bluetooth)
//! - ALAC encoding for realtime streaming
//! - AAC encoding for buffered streaming
//! - RTP packet formatting and transmission
//! - Audio buffer management
//! - Retransmission handling

mod buffer;
pub mod cipher;
mod decoder;
mod encoder;
pub mod eq;
mod live_decoder;
mod rtp;
pub mod spatial;
mod streamer;
mod traits;

pub use buffer::{AudioBuffer, AudioFrame};
pub use decoder::{AudioDecoder, DecodedFrame};
pub use encoder::{create_encoder, AacEncoder, AlacEncoder, AudioEncoder, EncodedPacket};
pub use eq::{EqConfig, EqParams, Equalizer};
pub use live_decoder::{LiveAudioDecoder, LiveFrameSender, LivePcmFrame};
pub use rtp::{
    build_retransmit_response, payload_types, RetransmitRequest, RtpHeader, RtpPacket, RtpReceiver,
    RtpSender,
};
pub use spatial::{
    Position, SpatialMixer, SpatialMode, SpatialParams, SpatialSnapshot, SpeakerConfig,
    SpeakerParams,
};
pub use streamer::AudioStreamer;
pub use traits::{AudioSource, EncoderTrait};
