//! PCM framing for the AirPlay feeder.
//!
//! The player owns PCM conversion and the bounded callback queue. This receiver
//! decodes the wire frame and resamples for transport. Samples are i16; this path
//! does not preserve 24-bit audio.

pub const TRANSPORT_FORMAT: &str = "ALAC 44100 Hz 16-bit stereo; input i16";
pub const INPUT_BITS: u8 = 16;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PcmFrame {
    pub epoch: u64,
    pub sample_rate: u32,
    pub channels: u16,
    pub samples: Vec<i16>,
}

#[cfg(test)]
fn encode_frame(frame: &PcmFrame) -> Vec<u8> {
    let mut bytes = Vec::with_capacity(18 + frame.samples.len() * 2);
    bytes.extend_from_slice(&frame.epoch.to_le_bytes());
    bytes.extend_from_slice(&frame.sample_rate.to_le_bytes());
    bytes.extend_from_slice(&frame.channels.to_le_bytes());
    bytes.extend_from_slice(&(frame.samples.len() as u32).to_le_bytes());
    for sample in &frame.samples {
        bytes.extend_from_slice(&sample.to_le_bytes());
    }
    bytes
}

pub fn decode_frame(bytes: &[u8]) -> Option<PcmFrame> {
    if bytes.len() < 18 {
        return None;
    }
    let epoch = u64::from_le_bytes(bytes[0..8].try_into().ok()?);
    let sample_rate = u32::from_le_bytes(bytes[8..12].try_into().ok()?);
    let channels = u16::from_le_bytes(bytes[12..14].try_into().ok()?);
    let count = u32::from_le_bytes(bytes[14..18].try_into().ok()?) as usize;
    let body = &bytes[18..];
    if channels == 0 || sample_rate == 0 || body.len() != count * 2 {
        return None;
    }
    let mut samples = Vec::with_capacity(count);
    for chunk in body.chunks_exact(2) {
        samples.push(i16::from_le_bytes([chunk[0], chunk[1]]));
    }
    Some(PcmFrame {
        epoch,
        sample_rate,
        channels,
        samples,
    })
}

/// Linear resample to the fixed 44100 Hz transport rate. Same-rate input is copied.
pub fn resample_stereo_i16(input: &[i16], from_rate: u32, to_rate: u32) -> Vec<i16> {
    if from_rate == 0 || to_rate == 0 || from_rate == to_rate || input.len() < 2 {
        return input.to_vec();
    }
    let frames = input.len() / 2;
    let out_frames = ((frames as u64) * (to_rate as u64) / (from_rate as u64)).max(1) as usize;
    let mut out = Vec::with_capacity(out_frames * 2);
    for index in 0..out_frames {
        let position = (index as f64) * (from_rate as f64) / (to_rate as f64);
        let left_index = (position.floor() as usize).min(frames - 1);
        let right_index = (left_index + 1).min(frames - 1);
        let fraction = (position - left_index as f64) as f32;
        for channel in 0..2 {
            let start = input[left_index * 2 + channel] as f32;
            let end = input[right_index * 2 + channel] as f32;
            let mixed = start + (end - start) * fraction;
            out.push(mixed.round().clamp(-32767.0, 32767.0) as i16);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn known_frame_bytes_match_the_player_tap() {
        let frame = PcmFrame {
            epoch: 1,
            sample_rate: 44_100,
            channels: 2,
            samples: vec![1, -1],
        };
        assert_eq!(
            encode_frame(&frame),
            vec![
                1, 0, 0, 0, 0, 0, 0, 0, 0x44, 0xAC, 0, 0, 2, 0, 2, 0, 0, 0, 1, 0, 0xFF, 0xFF
            ]
        );
    }

    #[test]
    fn resample_keeps_same_rate_and_stretches_a_slower_rate() {
        let input = vec![0, 0, 32767, -32767];
        assert_eq!(resample_stereo_i16(&input, 44_100, 44_100), input);
        let stretched = resample_stereo_i16(&input, 22_050, 44_100);
        assert!(stretched.len() >= input.len());
    }

    #[test]
    fn frame_roundtrip_rejects_a_short_body() {
        let frame = PcmFrame {
            epoch: 7,
            sample_rate: 48_000,
            channels: 2,
            samples: vec![1, -2, 3, -4],
        };
        let bytes = encode_frame(&frame);
        assert_eq!(decode_frame(&bytes), Some(frame));
        assert_eq!(decode_frame(&bytes[..bytes.len() - 1]), None);
    }
}
