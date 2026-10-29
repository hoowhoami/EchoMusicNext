use crate::config::SpectrumConfig;
use crate::events::{FrequencyDataInfo, SpectrumFrame};
use rustfft::{
    Fft, FftPlanner,
    num_complex::{Complex32, Complex64},
};
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

#[cfg(test)]
const SPECTRUM_FFT_SIZE: usize = 2048;
pub static CAPTURE_ENABLED: std::sync::atomic::AtomicBool =
    std::sync::atomic::AtomicBool::new(false);
const SPECTRUM_FLOOR_DB: f64 = -74.0;
// Keep the pre-calibration visual threshold: a 0.5-amplitude bin-centered sine is near full scale.
const SPECTRUM_CEILING_DB: f64 = -6.0;
const HANN_COHERENT_GAIN: f64 = 0.5;
const FREQUENCY_FFT_SIZE: usize = 2048;
const FREQUENCY_BIN_COUNT: usize = FREQUENCY_FFT_SIZE / 2;

#[derive(Debug)]
pub struct SampleRing {
    samples: Vec<f32>,
    cursor: usize,
    filled: bool,
    // Preallocated at ring creation: the realtime callback never allocates.
    frequency_samples: Vec<f32>,
    frequency_capture: bool,
    frequency_sequence: u64,
    frequency_generation: u64,
}

impl SampleRing {
    pub fn new(capacity: usize) -> Self {
        Self {
            samples: vec![0.0; capacity.max(2)],
            cursor: 0,
            filled: false,
            frequency_samples: vec![0.0; FREQUENCY_FFT_SIZE],
            frequency_capture: false,
            frequency_sequence: 0,
            frequency_generation: 0,
        }
    }

    pub fn push_interleaved(&mut self, input: &[f32], channels: usize) {
        let channels = channels.max(1);
        for frame in input.chunks(channels) {
            self.samples[self.cursor] = downmix_for_spectrum(frame);
            if self.frequency_capture {
                let index = self.frequency_sequence as usize % FREQUENCY_FFT_SIZE;
                self.frequency_samples[index] = downmix_for_frequency(frame);
                self.frequency_sequence = self.frequency_sequence.wrapping_add(1);
            }
            self.cursor += 1;
            if self.cursor >= self.samples.len() {
                self.cursor = 0;
                self.filled = true;
            }
        }
    }

    pub fn clear(&mut self) {
        self.cursor = 0;
        self.filled = false;
        self.reset_frequency();
    }

    pub fn set_frequency_capture(&mut self, enabled: bool, reset: bool) {
        if self.frequency_capture != enabled || reset {
            self.frequency_capture = enabled;
            self.reset_frequency();
        }
    }

    fn reset_frequency(&mut self) {
        self.frequency_sequence = 0;
        self.frequency_generation = self.frequency_generation.wrapping_add(1);
        self.frequency_samples.fill(0.0);
    }

    fn latest_frequency_into(&self, output: &mut Vec<f32>) {
        output.clear();
        let count = self.frequency_sequence.min(FREQUENCY_FFT_SIZE as u64) as usize;
        if !self.frequency_capture || count == 0 {
            return;
        }
        let cursor = self.frequency_sequence as usize % FREQUENCY_FFT_SIZE;
        let start = (cursor + FREQUENCY_FFT_SIZE - count) % FREQUENCY_FFT_SIZE;
        let first_count = count.min(FREQUENCY_FFT_SIZE - start);
        output.extend_from_slice(&self.frequency_samples[start..start + first_count]);
        output.extend_from_slice(&self.frequency_samples[..count - first_count]);
    }

    #[cfg(test)]
    pub fn latest(&self, count: usize) -> Vec<f32> {
        let mut output = Vec::with_capacity(count.min(self.samples.len()));
        self.latest_into(count, &mut output);
        output
    }

    pub fn latest_into(&self, count: usize, output: &mut Vec<f32>) {
        output.clear();
        let available = if self.filled {
            self.samples.len()
        } else {
            self.cursor
        };
        let count = count.min(available);
        if count == 0 {
            return;
        }
        let start = (self.cursor + self.samples.len() - count) % self.samples.len();
        let first_count = count.min(self.samples.len() - start);
        output.extend_from_slice(&self.samples[start..start + first_count]);
        let remaining = count - first_count;
        if remaining > 0 {
            output.extend_from_slice(&self.samples[..remaining]);
        }
    }
}

fn downmix_for_spectrum(frame: &[f32]) -> f32 {
    frame
        .iter()
        .copied()
        .max_by(|left, right| {
            left.abs()
                .partial_cmp(&right.abs())
                .unwrap_or(std::cmp::Ordering::Equal)
        })
        .unwrap_or(0.0)
}

// Web Audio's speakers -> mono rules, including discrete fallback for layouts
// other than mono/stereo/quad/5.1. The legacy maximum-channel mix stays intact.
fn downmix_for_frequency(frame: &[f32]) -> f32 {
    match frame.len() {
        2 => 0.5 * (frame[0] + frame[1]),
        4 => 0.25 * (frame[0] + frame[1] + frame[2] + frame[3]),
        6 => {
            std::f32::consts::FRAC_1_SQRT_2 * (frame[0] + frame[1])
                + frame[2]
                + 0.5 * (frame[4] + frame[5])
        }
        _ => frame.first().copied().unwrap_or(0.0),
    }
}

struct FrequencyAnalyzer {
    fft: Arc<dyn Fft<f64>>,
    buffer: Vec<Complex64>,
    scratch: Vec<Complex64>,
    window: Vec<f64>,
    previous: Vec<f64>,
    bytes: Vec<u8>,
    last_sequence: Option<u64>,
    next_sequence: u64,
    generation: u64,
    sample_rate: u32,
    #[cfg(test)]
    fft_runs: usize,
}

impl FrequencyAnalyzer {
    fn new() -> Self {
        let fft = FftPlanner::<f64>::new().plan_fft_forward(FREQUENCY_FFT_SIZE);
        let scratch = vec![Complex64::ZERO; fft.get_inplace_scratch_len()];
        Self {
            fft,
            buffer: vec![Complex64::ZERO; FREQUENCY_FFT_SIZE],
            scratch,
            window: (0..FREQUENCY_FFT_SIZE)
                .map(|i| {
                    let phase = 2.0 * std::f64::consts::PI * i as f64 / FREQUENCY_FFT_SIZE as f64;
                    0.42 - 0.5 * phase.cos() + 0.08 * (2.0 * phase).cos()
                })
                .collect(),
            previous: vec![0.0; FREQUENCY_BIN_COUNT],
            bytes: vec![0; FREQUENCY_BIN_COUNT],
            last_sequence: None,
            next_sequence: 0,
            generation: 0,
            sample_rate: 0,
            #[cfg(test)]
            fft_runs: 0,
        }
    }

    fn update(
        &mut self,
        samples: &[f32],
        sample_rate: u32,
        sequence: u64,
        generation: u64,
        fps: u32,
    ) -> Vec<u8> {
        if self.generation != generation || self.sample_rate != sample_rate {
            self.previous.fill(0.0);
            self.bytes.fill(0);
            self.last_sequence = None;
            self.generation = generation;
            self.sample_rate = sample_rate;
        }
        // Only the opted-in cadence advances this profile. Repeated reads of
        // one PCM snapshot reuse the bytes, even with faster legacy consumers.
        let interval = u64::from(sample_rate).div_ceil(u64::from(fps.max(1)));
        if self.last_sequence.is_some() && sequence < self.next_sequence {
            return self.bytes.clone();
        }
        self.buffer.fill(Complex64::ZERO);
        let samples = &samples[samples.len().saturating_sub(FREQUENCY_FFT_SIZE)..];
        let offset = FREQUENCY_FFT_SIZE - samples.len();
        for (i, sample) in samples.iter().enumerate() {
            self.buffer[offset + i].re = f64::from(*sample) * self.window[offset + i];
        }
        self.fft
            .process_with_scratch(&mut self.buffer, &mut self.scratch);
        #[cfg(test)]
        {
            self.fft_runs += 1;
        }
        for k in 0..FREQUENCY_BIN_COUNT {
            let magnitude = self.buffer[k].norm() / FREQUENCY_FFT_SIZE as f64;
            let smoothed = 0.6 * self.previous[k] + 0.4 * magnitude;
            // The specification resets non-finite smoothed magnitudes to 0.
            self.previous[k] = if smoothed.is_finite() { smoothed } else { 0.0 };
            let db = 20.0 * self.previous[k].log10();
            self.bytes[k] = ((db + 100.0) * 255.0 / 70.0).floor().clamp(0.0, 255.0) as u8;
        }
        self.next_sequence = if self.last_sequence.is_none() {
            sequence.saturating_add(interval.max(1))
        } else {
            self.next_sequence.saturating_add(
                ((sequence - self.next_sequence) / interval.max(1) + 1) * interval.max(1),
            )
        };
        self.last_sequence = Some(sequence);
        self.bytes.clone()
    }
}

pub struct SpectrumAnalyzer {
    config: SpectrumConfig,
    previous: Vec<f64>,
    fft: Arc<dyn Fft<f32>>,
    buffer: Vec<Complex32>,
    snapshot: Vec<f32>,
    frequency: Option<FrequencyAnalyzer>,
    frequency_snapshot: Vec<f32>,
    frequency_sequence: u64,
    frequency_generation: u64,
}

impl SpectrumAnalyzer {
    pub fn new(config: SpectrumConfig) -> Self {
        Self::with_frequency(config, None)
    }

    fn with_frequency(config: SpectrumConfig, frequency: Option<FrequencyAnalyzer>) -> Self {
        let mut planner = FftPlanner::<f32>::new();
        let size = config.fft_size;
        let fft = planner.plan_fft_forward(size);
        let frequency = if config.include_frequency_data {
            Some(frequency.unwrap_or_else(FrequencyAnalyzer::new))
        } else {
            None
        };
        let frequency_capacity = if frequency.is_some() {
            FREQUENCY_FFT_SIZE
        } else {
            0
        };
        Self {
            previous: vec![0.0; config.bands],
            config,
            fft,
            buffer: vec![Complex32::ZERO; size],
            snapshot: Vec::with_capacity(size),
            frequency,
            frequency_snapshot: Vec::with_capacity(frequency_capacity),
            frequency_sequence: 0,
            frequency_generation: 0,
        }
    }

    pub fn reconfigure(&mut self, config: SpectrumConfig, reset_frequency: bool) {
        let frequency = if reset_frequency {
            None
        } else {
            self.frequency.take()
        };
        *self = Self::with_frequency(config, frequency);
    }

    #[cfg(test)]
    pub fn analyze(&mut self, ring: &mut SampleRing, sample_rate: u32) -> SpectrumFrame {
        self.snapshot_ring(ring);
        self.analyze_snapshot(sample_rate)
    }

    pub fn snapshot_ring(&mut self, ring: &mut SampleRing) {
        ring.set_frequency_capture(self.frequency.is_some(), false);
        ring.latest_into(self.config.fft_size, &mut self.snapshot);
        if self.frequency.is_some() {
            ring.latest_frequency_into(&mut self.frequency_snapshot);
            self.frequency_sequence = ring.frequency_sequence;
            self.frequency_generation = ring.frequency_generation;
        }
    }

    pub fn analyze_snapshot(&mut self, sample_rate: u32) -> SpectrumFrame {
        let snapshot = std::mem::take(&mut self.snapshot);
        let mut frame = self.analyze_samples(&snapshot, sample_rate);
        if let Some(frequency) = self.frequency.as_mut() {
            frame.frequency_data = Some(frequency.update(
                &self.frequency_snapshot,
                sample_rate,
                self.frequency_sequence,
                self.frequency_generation,
                self.config.frequency_fps,
            ));
            frame.frequency_data_info = Some(FrequencyDataInfo {
                fft_size: FREQUENCY_FFT_SIZE as u32,
                frequency_bin_count: FREQUENCY_BIN_COUNT as u32,
                sample_rate,
                bin_width_hz: f64::from(sample_rate) / FREQUENCY_FFT_SIZE as f64,
                window: "blackman",
                fft_normalization: "1/N",
                smoothing_time_constant: 0.6,
                min_decibels: -100.0,
                max_decibels: -30.0,
                channel_interpretation: "speakers",
                frames_per_second: self.config.frequency_fps,
            });
        }
        self.snapshot = snapshot;
        frame
    }

    pub fn analyze_samples(&mut self, window: &[f32], sample_rate: u32) -> SpectrumFrame {
        let mut bins = vec![0.0; self.config.bands];
        if !window.is_empty() {
            self.prepare_fft_buffer(window);
            self.fft.process(&mut self.buffer);
            for (bin, value) in bins.iter_mut().enumerate() {
                let magnitude = self.band_magnitude(bin, sample_rate);
                let smoothed = self.previous[bin] * self.config.smoothing
                    + magnitude * (1.0 - self.config.smoothing);
                *value = smoothed.clamp(0.0, 1.0);
                self.previous[bin] = *value;
            }
        }
        let peak = window.iter().copied().map(f32::abs).fold(0.0f32, f32::max) as f64;
        let rms = if window.is_empty() {
            0.0
        } else {
            (window
                .iter()
                .map(|v| (*v as f64) * (*v as f64))
                .sum::<f64>()
                / window.len() as f64)
                .sqrt()
        };
        let waveform = self
            .config
            .include_waveform
            .then(|| sample_waveform(window));
        SpectrumFrame {
            sample_rate,
            fft_size: self.config.fft_size as u32,
            bins,
            waveform,
            frequency_data: None,
            frequency_data_info: None,
            peak,
            rms,
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|value| value.as_secs_f64())
                .unwrap_or_default(),
        }
    }

    fn prepare_fft_buffer(&mut self, window: &[f32]) {
        self.buffer.fill(Complex32::ZERO);
        let window = &window[window.len().saturating_sub(self.config.fft_size)..];
        let offset = self.config.fft_size.saturating_sub(window.len());
        for (index, sample) in window.iter().copied().enumerate() {
            let target = offset + index;
            let phase = target as f32 / (self.config.fft_size.saturating_sub(1).max(1)) as f32;
            let hann = 0.5 - 0.5 * (2.0 * std::f32::consts::PI * phase).cos();
            self.buffer[target].re = sample * hann;
        }
    }

    fn band_magnitude(&self, bin: usize, sample_rate: u32) -> f64 {
        if sample_rate < 4 {
            return 0.0;
        }
        let (min_freq, max_freq) = self.band_frequency_range(bin);
        let nyquist = sample_rate as f64 * 0.5;
        if min_freq >= nyquist {
            return 0.0;
        }
        let min_freq = min_freq.clamp(1.0, nyquist - 1.0);
        let max_freq = max_freq.clamp(min_freq + 1.0, nyquist);
        let start = self.frequency_to_fft_bin(min_freq, sample_rate);
        let end = self
            .frequency_to_fft_bin(max_freq, sample_rate)
            .max(start + 1);
        let max_bin = (self.config.fft_size / 2).saturating_sub(1);
        let mut magnitude = 0.0f64;
        for index in start.min(max_bin)..=end.min(max_bin) {
            let value = self.fft_bin_amplitude(index);
            magnitude = magnitude.max(value);
        }
        magnitude_to_unit(magnitude)
    }

    fn fft_bin_amplitude(&self, index: usize) -> f64 {
        self.buffer[index].norm() as f64 / (self.config.fft_size as f64 * HANN_COHERENT_GAIN * 0.5)
    }

    fn frequency_to_fft_bin(&self, frequency: f64, sample_rate: u32) -> usize {
        ((frequency / sample_rate as f64) * self.config.fft_size as f64).round() as usize
    }

    fn band_frequency_range(&self, bin: usize) -> (f64, f64) {
        let bands = self.config.bands.max(1);
        let min_frequency = self.config.min_frequency.max(1.0);
        let max_frequency = self.config.max_frequency.max(min_frequency + 1.0);
        let lower_t = bin as f64 / bands as f64;
        let upper_t = (bin + 1) as f64 / bands as f64;
        if self.config.scale == "linear" {
            return (
                min_frequency + (max_frequency - min_frequency) * lower_t,
                min_frequency + (max_frequency - min_frequency) * upper_t,
            );
        }
        if self.config.scale == "mel" {
            let low = 2595.0 * (1.0 + min_frequency / 700.0).log10();
            let high = 2595.0 * (1.0 + max_frequency / 700.0).log10();
            let at = |t: f64| 700.0 * (10.0_f64.powf((low + (high - low) * t) / 2595.0) - 1.0);
            return (at(lower_t), at(upper_t));
        }
        let ratio = max_frequency / min_frequency;
        (
            min_frequency * ratio.powf(lower_t),
            min_frequency * ratio.powf(upper_t),
        )
    }
}

fn sample_waveform(window: &[f32]) -> Vec<f64> {
    window
        .iter()
        .map(|sample| f64::from(sample.clamp(-1.0, 1.0)))
        .collect()
}

#[cfg(test)]
fn frequency_to_fft_bin(frequency: f64, sample_rate: u32) -> usize {
    ((frequency / sample_rate as f64) * SPECTRUM_FFT_SIZE as f64).round() as usize
}

fn magnitude_to_unit(magnitude: f64) -> f64 {
    if magnitude <= f64::EPSILON {
        return 0.0;
    }
    let db = 20.0 * magnitude.log10();
    ((db - SPECTRUM_FLOOR_DB) / (SPECTRUM_CEILING_DB - SPECTRUM_FLOOR_DB)).clamp(0.0, 1.0)
}

#[cfg(test)]
mod tests {
    use super::*;

    // Independent direct DFT oracle for the W3C AnalyserNode equations. This
    // intentionally does not use rustfft or the production window helper.
    fn web_audio_oracle(samples: &[f32], previous: &mut [f64]) -> Vec<u8> {
        let n = 2048;
        (0..n / 2)
            .map(|k| {
                let (mut real, mut imaginary) = (0.0, 0.0);
                for (i, sample) in samples.iter().enumerate() {
                    let phase = 2.0 * std::f64::consts::PI * i as f64 / n as f64;
                    let blackman = 0.42 - 0.5 * phase.cos() + 0.08 * (2.0 * phase).cos();
                    let angle = phase * k as f64;
                    real += f64::from(*sample) * blackman * angle.cos();
                    imaginary -= f64::from(*sample) * blackman * angle.sin();
                }
                let magnitude = (real * real + imaginary * imaginary).sqrt() / n as f64;
                previous[k] = 0.6 * previous[k] + 0.4 * magnitude;
                ((20.0 * previous[k].log10() + 100.0) * 255.0 / 70.0)
                    .floor()
                    .clamp(0.0, 255.0) as u8
            })
            .collect()
    }

    #[test]
    fn frequency_data_matches_w3c_direct_dft_and_amplitude_smoothing() {
        let samples = (0..2048)
            .map(|i| {
                let phase = 2.0 * std::f64::consts::PI * i as f64 / 2048.0;
                (0.003 * (43.0 * phase).sin() + 0.001 * (137.25 * phase).cos()) as f32
            })
            .collect::<Vec<_>>();
        let mut oracle_previous = vec![0.0; 1024];
        let mut analyzer = FrequencyAnalyzer::new();
        for (sequence, input) in [(2048, samples), (4096, vec![0.0; 2048])] {
            let expected = web_audio_oracle(&input, &mut oracle_previous);
            let actual = analyzer.update(&input, 48_000, sequence, 1, 30);
            assert_eq!(actual, expected);
        }
    }

    #[test]
    fn frequency_data_uses_periodic_blackman_fft_normalization_and_floor() {
        let mut input = vec![0.0; 2048];
        input[512] = 0.5; // Blackman(1/4) = 0.34 exactly, not an N-1 window.
        let mut analyzer = FrequencyAnalyzer::new();
        let actual = analyzer.update(&input, 48_000, 2048, 1, 60);
        let expected = ((20.0_f64 * (0.4_f64 * 0.5 * 0.34 / 2048.0).log10() + 100.0) * 255.0 / 70.0)
            .floor() as u8;
        assert_eq!(actual, vec![expected; 1024]);
        assert_eq!(expected, 37);
    }

    #[test]
    fn frequency_speakers_downmix_preserves_the_legacy_antiphase_pipeline() {
        assert_eq!(downmix_for_frequency(&[0.75, -0.75]), 0.0);
        assert_eq!(downmix_for_frequency(&[1.0, 0.0, -0.5, 0.5]), 0.25);
        let surround = downmix_for_frequency(&[1.0, 0.5, 0.25, 99.0, 0.5, -0.25]);
        assert!((surround - (1.5 * 0.5_f32.sqrt() + 0.25 + 0.125)).abs() < 1e-6);
        assert_eq!(downmix_for_frequency(&[0.25, 9.0, 9.0]), 0.25);
        assert_eq!(downmix_for_frequency(&[0.5; 8]), 0.5);
        let mut ring = SampleRing::new(4096);
        ring.set_frequency_capture(true, false);
        ring.push_interleaved(&[0.75, -0.75].repeat(2048), 2);
        let mut analyzer = SpectrumAnalyzer::new(SpectrumConfig {
            fft_size: 256,
            include_waveform: true,
            include_frequency_data: true,
            ..SpectrumConfig::default()
        });
        let frame = analyzer.analyze(&mut ring, 48_000);
        assert_eq!(frame.waveform.unwrap().len(), 256);
        assert_eq!(frame.peak, 0.75);
        assert_eq!(frame.frequency_data.unwrap(), vec![0; 1024]);
        assert_eq!(analyzer.frequency_snapshot.len(), 2048);
    }

    #[test]
    fn frequency_profile_and_cadence_are_independent_of_legacy_subscriptions() {
        let mut ring = SampleRing::new(8192);
        ring.set_frequency_capture(true, false);
        ring.push_interleaved(&vec![0.001; 2048], 1);
        let mut analyzer = SpectrumAnalyzer::new(SpectrumConfig {
            include_frequency_data: true,
            frequency_fps: 30,
            ..SpectrumConfig::default()
        });
        let first = analyzer.analyze(&mut ring, 48_000);
        assert_eq!(analyzer.frequency.as_ref().unwrap().fft_runs, 1);
        // Reading the same PCM, or a higher-fps legacy subscriber, must not
        // apply smoothing twice or advance this independent profile.
        ring.push_interleaved(&vec![0.001; 400], 1);
        analyzer.reconfigure(
            SpectrumConfig {
                fft_size: 8192,
                smoothing: 0.98,
                fps: 120,
                include_frequency_data: true,
                frequency_fps: 30,
                ..SpectrumConfig::default()
            },
            false,
        );
        let repeated = analyzer.analyze(&mut ring, 48_000);
        assert_eq!(first.frequency_data, repeated.frequency_data);
        assert_eq!(analyzer.frequency.as_ref().unwrap().fft_runs, 1);
        ring.push_interleaved(&vec![0.001; 1200], 1);
        let next = analyzer.analyze(&mut ring, 48_000);
        assert_ne!(first.frequency_data, next.frequency_data);
        assert_eq!(analyzer.frequency.as_ref().unwrap().fft_runs, 2);
        analyzer.reconfigure(SpectrumConfig::default(), false);
        assert!(analyzer.frequency.is_none());
        let legacy = analyzer.analyze(&mut ring, 48_000);
        assert!(legacy.frequency_data.is_none());
        assert!(legacy.frequency_data_info.is_none());
        assert!(!ring.frequency_capture);
    }

    #[test]
    fn frequency_capture_reset_and_sample_rate_change_retire_previous_amplitudes() {
        let mut analyzer = FrequencyAnalyzer::new();
        let input = vec![0.001; 2048];
        let first = analyzer.update(&input, 48_000, 2048, 1, 30);
        let warm = analyzer.update(&input, 48_000, 4096, 1, 30);
        assert_ne!(first, warm);
        assert_eq!(first, analyzer.update(&input, 48_000, 2048, 2, 30));
        assert_eq!(first, analyzer.update(&input, 44_100, 4096, 2, 30));
        let silence = analyzer.update(&vec![0.0; 2048], 44_100, 2048, 3, 30);
        assert_eq!(silence, vec![0; 1024]);
        let mut ring = SampleRing::new(4096);
        ring.set_frequency_capture(true, false);
        ring.push_interleaved(&input, 1);
        ring.clear();
        let mut snapshot = Vec::new();
        ring.latest_frequency_into(&mut snapshot);
        assert!(snapshot.is_empty());
        ring.set_frequency_capture(false, false);
        ring.push_interleaved(&input, 1);
        ring.set_frequency_capture(true, false);
        ring.latest_frequency_into(&mut snapshot);
        assert!(snapshot.is_empty());
    }

    #[test]
    fn frequency_cadence_does_not_drift_with_quantized_output_callbacks() {
        let mut analyzer = FrequencyAnalyzer::new();
        let input = vec![0.001; 2048];
        for callback in 0..101 {
            analyzer.update(&input, 48_000, callback * 768, 1, 30);
        }
        // 1.6 s of PCM at 30 Hz, plus the initial snapshot. Advancing the
        // deadline from the latest callback instead would incorrectly yield
        // only 34 transforms here because 1600 is not divisible by 768.
        assert_eq!(analyzer.fft_runs, 49);
    }

    #[test]
    fn frequency_options_and_wire_metadata_are_explicit_and_opt_in() {
        let options = serde_json::from_value(serde_json::json!({
            "include_frequency_data": true, "frequency_fps": 999,
            "fft_size": 256, "smoothing": 0.98
        }))
        .unwrap();
        let config = SpectrumConfig::from_options(Some(options));
        assert!(config.include_frequency_data);
        assert_eq!(config.frequency_fps, 60);
        let mut ring = SampleRing::new(256);
        ring.set_frequency_capture(true, false);
        ring.push_interleaved(&vec![0.001; 4096], 1);
        let mut analyzer = SpectrumAnalyzer::new(config);
        let wire = serde_json::to_value(analyzer.analyze(&mut ring, 44_100)).unwrap();
        assert_eq!(wire["fft_size"], 256);
        assert_eq!(wire["frequency_data"].as_array().unwrap().len(), 1024);
        let info = &wire["frequency_data_info"];
        assert_eq!(info["fftSize"], 2048);
        assert_eq!(info["frequencyBinCount"], 1024);
        assert_eq!(info["sampleRate"], 44100);
        assert_eq!(info["binWidthHz"], 44100.0 / 2048.0);
        assert_eq!(info["framesPerSecond"], 60);
        assert_eq!(info["window"], "blackman");
        assert_eq!(info["fftNormalization"], "1/N");
        assert_eq!(info["smoothingTimeConstant"], 0.6);
        assert_eq!(info["minDecibels"], -100.0);
        assert_eq!(info["maxDecibels"], -30.0);
        assert_eq!(info["channelInterpretation"], "speakers");
        analyzer.reconfigure(SpectrumConfig::default(), false);
        let legacy = serde_json::to_value(analyzer.analyze(&mut ring, 44_100)).unwrap();
        assert!(legacy.get("frequency_data").is_none());
        assert!(legacy.get("frequency_data_info").is_none());
    }

    #[test]
    fn frequency_nonfinite_magnitudes_reset_to_zero_and_short_input_is_left_padded() {
        let mut analyzer = FrequencyAnalyzer::new();
        let full = vec![0.001; 2048];
        assert!(
            analyzer
                .update(&full, 48_000, 2048, 1, 30)
                .iter()
                .any(|b| *b > 0)
        );
        let mut invalid = full;
        invalid[1000] = f32::NAN;
        assert_eq!(
            analyzer.update(&invalid, 48_000, 4096, 1, 30),
            vec![0; 1024]
        );
        let short = [0.001, -0.001];
        let mut padded = vec![0.0; 2048];
        padded[2046..].copy_from_slice(&short);
        let mut short_analyzer = FrequencyAnalyzer::new();
        let mut full_analyzer = FrequencyAnalyzer::new();
        assert_eq!(
            short_analyzer.update(&short, 48_000, 2, 1, 30),
            full_analyzer.update(&padded, 48_000, 2, 1, 30)
        );
    }

    #[test]
    fn sample_ring_keeps_latest_mono_samples() {
        let mut ring = SampleRing::new(4);
        ring.push_interleaved(&[1.0, 0.0, 0.5, -0.5], 2);
        ring.push_interleaved(&[0.25, -0.25], 2);

        assert_eq!(ring.latest(3), vec![1.0, -0.5, -0.25]);
    }

    #[test]
    fn sample_ring_does_not_cancel_opposite_phase_channels() {
        let mut ring = SampleRing::new(2);
        ring.push_interleaved(&[0.75, -0.75], 2);

        assert_eq!(ring.latest(1), vec![-0.75]);
    }

    #[test]
    fn sample_ring_latest_into_reuses_capacity_across_wrap() {
        let mut ring = SampleRing::new(4);
        ring.push_interleaved(&[1.0, 2.0, 3.0, 4.0, 5.0], 1);
        let mut output = Vec::with_capacity(4);
        let capacity = output.capacity();

        ring.latest_into(4, &mut output);

        assert_eq!(output, vec![2.0, 3.0, 4.0, 5.0]);
        assert_eq!(output.capacity(), capacity);
    }

    #[test]
    fn analyzer_outputs_nonzero_bins_for_tone() {
        let sample_rate = 48_000;
        let mut ring = SampleRing::new(SPECTRUM_FFT_SIZE);
        let mut samples = Vec::with_capacity(SPECTRUM_FFT_SIZE * 2);
        for index in 0..SPECTRUM_FFT_SIZE {
            let phase = 2.0 * std::f32::consts::PI * 1_000.0 * index as f32 / sample_rate as f32;
            let sample = phase.sin() * 0.5;
            samples.push(sample);
            samples.push(sample);
        }
        ring.push_interleaved(&samples, 2);

        let mut analyzer = SpectrumAnalyzer::new(SpectrumConfig {
            bands: 64,
            min_frequency: 40.0,
            max_frequency: 16_000.0,
            smoothing: 0.0,
            ..SpectrumConfig::default()
        });
        let frame = analyzer.analyze(&mut ring, sample_rate);

        assert!(frame.peak > 0.4);
        assert!(frame.rms > 0.2);
        assert!(frame.bins.iter().any(|value| *value > 0.2));
        assert!(frame.waveform.is_none());
    }

    #[test]
    fn analyzer_includes_bounded_waveform_only_when_requested() {
        let window = (0..SPECTRUM_FFT_SIZE)
            .map(|index| if index % 2 == 0 { 1.5 } else { -1.5 })
            .collect::<Vec<_>>();
        let mut analyzer = SpectrumAnalyzer::new(SpectrumConfig {
            include_waveform: true,
            ..SpectrumConfig::default()
        });

        let frame = analyzer.analyze_samples(&window, 48_000);
        let waveform = frame.waveform.expect("waveform should be present");

        assert_eq!(waveform.len(), SPECTRUM_FFT_SIZE);
        assert!(waveform.iter().all(|value| (-1.0..=1.0).contains(value)));
        assert_eq!(waveform[0], 1.0);
        assert_eq!(waveform[1], -1.0);
    }

    #[test]
    fn hann_window_reports_bin_centered_tone_amplitude() {
        let mut analyzer = SpectrumAnalyzer::new(SpectrumConfig::default());
        let bin = 43usize;
        let amplitude = 0.5f32;
        let window = (0..SPECTRUM_FFT_SIZE)
            .map(|index| {
                let phase = 2.0 * std::f32::consts::PI * bin as f32 * index as f32
                    / SPECTRUM_FFT_SIZE as f32;
                phase.sin() * amplitude
            })
            .collect::<Vec<_>>();

        analyzer.prepare_fft_buffer(&window);
        analyzer.fft.process(&mut analyzer.buffer);

        assert!((analyzer.fft_bin_amplitude(bin) - f64::from(amplitude)).abs() < 0.002);
        assert!(magnitude_to_unit(f64::from(amplitude)) > 0.99);
        assert!(magnitude_to_unit(f64::from(amplitude) * 0.5) < 0.95);
    }

    #[test]
    fn configurable_fft_and_frequency_scales_locate_the_actual_tone() {
        for size in [256, 1024, 2048, 4096, 8192] {
            for scale in ["log", "linear", "mel"] {
                let mut analyzer = SpectrumAnalyzer::new(SpectrumConfig {
                    fft_size: size,
                    bands: 128,
                    min_frequency: 20.,
                    max_frequency: 20000.,
                    smoothing: 0.,
                    include_waveform: true,
                    scale: scale.into(),
                    ..SpectrumConfig::default()
                });
                let samples = (0..size)
                    .map(|i| (2. * std::f32::consts::PI * 1000. * i as f32 / 48000.).sin() * 0.2)
                    .collect::<Vec<_>>();
                let frame = analyzer.analyze_samples(&samples, 48000);
                assert_eq!(frame.fft_size, size as u32);
                assert_eq!(frame.sample_rate, 48000);
                assert_eq!(frame.bins.len(), 128);
                assert_eq!(frame.waveform.as_ref().unwrap().len(), size);
                assert!((frame.rms - 0.2 / 2.0_f64.sqrt()).abs() < 0.01);
                // The maximum may share adjacent low-resolution bands.
                let maximum = frame.bins.iter().copied().fold(0.0_f64, f64::max);
                let closest = (0..128)
                    .filter(|i| frame.bins[*i] >= maximum - 1e-6)
                    .map(|i| {
                        let (low, high) = analyzer.band_frequency_range(i);
                        if low > 1000. {
                            low - 1000.
                        } else if high < 1000. {
                            1000. - high
                        } else {
                            0.
                        }
                    })
                    .fold(f64::INFINITY, f64::min);
                assert!(
                    closest < 48000. / size as f64 * 2.,
                    "{scale} {size} {closest}"
                );
                let silence = analyzer.analyze_samples(&vec![0.; size], 48000);
                assert_eq!(silence.peak, 0.);
                assert_eq!(silence.rms, 0.);
                assert!(silence.bins.iter().all(|v| *v == 0.));
            }
        }
    }

    #[test]
    fn bands_above_output_nyquist_are_silent_not_a_panic() {
        let mut analyzer = SpectrumAnalyzer::new(SpectrumConfig {
            min_frequency: 10000.,
            max_frequency: 20000.,
            ..SpectrumConfig::default()
        });
        let frame = analyzer.analyze_samples(&vec![0.1; 1024], 8000);
        assert!(frame.bins.iter().all(|value| *value == 0.));
    }

    #[test]
    fn calibrated_spectrum_keeps_the_pre_compensation_visual_range() {
        let floor_magnitude = 10.0_f64.powf(SPECTRUM_FLOOR_DB / 20.0);
        let ceiling_magnitude = 10.0_f64.powf(SPECTRUM_CEILING_DB / 20.0);

        assert!(magnitude_to_unit(floor_magnitude).abs() < f64::EPSILON);
        assert!((magnitude_to_unit(ceiling_magnitude) - 1.0).abs() < f64::EPSILON);
    }
}
