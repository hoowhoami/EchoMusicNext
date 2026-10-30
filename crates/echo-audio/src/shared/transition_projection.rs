//! Read-only presentation of PCM already scheduled by the real transition runner.
//! Decode read-ahead is `queued`; only consumed output crosses the handover. The
//! callback only advances one atomic frame cursor and never locks this metadata.
use super::*;
use crate::transition::plan::PlanTemplate;
use crate::transition::presentation_cue::TransitionPresentationCue;

// Process-local identities do not alias when a replacement SharedAudio starts
// at generation zero with the same caller-provided track/request sequences.
static NEXT_TRANSITION_EPOCH: AtomicU64 = AtomicU64::new(1);

#[derive(serde::Serialize, Clone, Copy, Debug, PartialEq, Eq)]
#[serde(rename_all = "kebab-case")]
pub enum TransitionPlaybackPhase {
    Queued,
    Mixing,
    Tail,
    Completed,
}

#[derive(serde::Serialize, Clone, Debug, PartialEq)]
pub struct TransitionPlaybackSnapshot {
    #[serde(serialize_with = "identity")]
    pub epoch: u64,
    #[serde(serialize_with = "identity")]
    pub generation: u64,
    #[serde(serialize_with = "optional_identity")]
    pub request_id: Option<u64>,
    #[serde(serialize_with = "identity")]
    pub outgoing_seq: u64,
    #[serde(serialize_with = "identity")]
    pub incoming_seq: u64,
    pub mode: String,
    pub phase: TransitionPlaybackPhase,
    pub handed_over: bool,
    /// Consumed output frames / the mixer's real overlap frame budget. Early
    /// releases can complete before 1; effect tails can continue after 1.
    pub progress: f64,
    pub output_frames: u64,
    pub overlap_frames: u64,
    pub incoming_boundary_secs: f64,
    /// The existing output clock; absent until this source has taken over.
    pub incoming_position_secs: Option<f64>,
    pub outgoing_position_secs: Option<f64>,
    pub outgoing_end_secs: Option<f64>,
    pub outgoing_tempo_ratio: f64,
    pub output_speed: f64,
    pub sample_rate: u32,
    pub template: Option<PlanTemplate>,
    pub presentation_cue: TransitionPresentationCue,
    pub cue_elapsed_secs: f64,
    pub cue_duration_secs: f64,
}

fn identity<S: serde::Serializer>(value: &u64, serializer: S) -> Result<S::Ok, S::Error> {
    serializer.serialize_str(&value.to_string())
}
fn optional_identity<S: serde::Serializer>(
    value: &Option<u64>,
    serializer: S,
) -> Result<S::Ok, S::Error> {
    match value {
        Some(value) => serializer.serialize_some(&value.to_string()),
        None => serializer.serialize_none(),
    }
}

#[derive(Clone, Copy, Debug)]
pub(crate) struct TransitionPresentationPlan {
    pub outgoing_start_secs: f64,
    pub outgoing_end_secs: f64,
    pub outgoing_tempo_ratio: f64,
    pub output_speed: f64,
    pub sample_rate: u32,
    pub template: Option<PlanTemplate>,
    pub cue: TransitionPresentationCue,
}

#[derive(Clone, Copy)]
pub(crate) struct TransitionProjectionPlan {
    pub request_id: u64,
    pub generation: u64,
    pub outgoing_seq: u64,
    pub overlap_frames: u64,
    pub presentation: Option<TransitionPresentationPlan>,
}

pub(super) struct TransitionProjection {
    epoch: u64,
    generation: u64,
    request_id: Option<u64>,
    outgoing_seq: u64,
    incoming_seq: u64,
    mode: String,
    start_frame: u64,
    overlap_frames: u64,
    end_frame: Option<u64>,
    incoming_boundary_secs: f64,
    presentation: Option<TransitionPresentationPlan>,
}

#[derive(Default)]
pub(super) struct TransitionProjectionState {
    active: Option<TransitionProjection>,
}

impl SharedAudio {
    pub(crate) fn mark_mix_boundary(&self, info: TrackSwitchInfo, plan: TransitionProjectionPlan) {
        self.mark_track_boundary_inner(info, 0, false, Some(plan));
    }

    // Caller holds gapless_boundary, which serializes this output queue/cursor
    // snapshot with the callback's pop. Producers may allocate metadata here.
    pub(super) fn record_transition_boundary(
        &self,
        info: &TrackSwitchInfo,
        queued_samples: usize,
        plan: Option<TransitionProjectionPlan>,
    ) {
        if self.should_stop_decoding()
            || plan.is_some_and(|plan| !self.is_decode_generation_current(plan.generation))
        {
            return;
        }
        let Ok(mut state) = self.transition_projection.lock() else {
            return;
        };
        let Some(transition) = info.transition.as_ref() else {
            state.active = None;
            return;
        };
        let generation = self.current_decode_generation();
        if plan.is_some_and(|plan| plan.generation != generation) {
            return;
        }
        state.active = Some(TransitionProjection {
            epoch: NEXT_TRANSITION_EPOCH.fetch_add(1, Ordering::Relaxed),
            generation,
            request_id: plan.map(|plan| plan.request_id),
            outgoing_seq: plan.map_or_else(|| self.current_track_seq(), |plan| plan.outgoing_seq),
            incoming_seq: info.seq,
            mode: transition.mode.clone(),
            start_frame: self
                .output_frames_consumed
                .load(Ordering::Acquire)
                .saturating_add((queued_samples / self.mix_format.channels.max(1)) as u64),
            overlap_frames: plan.map_or_else(
                || {
                    (transition.overlap_secs.max(0.0)
                        * f64::from(self.mix_format.sample_rate.max(1))
                        / f64::from(self.speed()))
                    .round() as u64
                },
                |plan| plan.overlap_frames,
            ),
            end_frame: None,
            incoming_boundary_secs: info.start_position_secs,
            presentation: plan.and_then(|plan| plan.presentation),
        });
    }

    pub(crate) fn mark_mix_end(&self, gain_db: f32, request_id: u64, generation: u64) {
        self.mark_gain_marker_inner(gain_db, Some((request_id, generation)));
    }

    // Caller holds gain_marker, also held throughout each callback pop/cursor
    // advance. The end is the real Promote boundary, including releases/tails.
    pub(super) fn record_transition_end(
        &self,
        request_id: u64,
        generation: u64,
        queued_samples: usize,
    ) -> bool {
        if self.should_stop_decoding() {
            return false;
        }
        let Ok(mut state) = self.transition_projection.lock() else {
            return false;
        };
        if let Some(active) = state.active.as_mut() {
            if active.generation == generation
                && active.request_id == Some(request_id)
                && self.is_decode_generation_current(generation)
            {
                active.end_frame = Some(
                    self.output_frames_consumed
                        .load(Ordering::Acquire)
                        .saturating_add((queued_samples / self.mix_format.channels.max(1)) as u64),
                );
                return true;
            }
        }
        false
    }

    pub(super) fn clear_transition_projection(&self) {
        if let Ok(mut state) = self.transition_projection.lock() {
            state.active = None;
        }
    }

    /// This is an output-boundary projection, not a prediction from a queue,
    /// wall-clock interpolation, decoder position, or a gain crossover.
    pub fn transition_playback(&self) -> Option<TransitionPlaybackSnapshot> {
        let state = self.transition_projection.lock().ok()?;
        let active = state.active.as_ref()?;
        for _ in 0..3 {
            let generation = self.current_decode_generation();
            let seq = self.current_track_seq();
            let frame = self.output_frames_consumed.load(Ordering::Acquire);
            let position = self.position_secs();
            if seq != self.current_track_seq() || generation != self.current_decode_generation() {
                continue;
            }
            if generation != active.generation
                || self.should_stop_decoding()
                || self.decode_failed.load(Ordering::Acquire)
                || (seq != active.outgoing_seq && seq != active.incoming_seq)
            {
                return None;
            }
            let handed_over = seq == active.incoming_seq && frame >= active.start_frame;
            let consumed = if handed_over {
                frame
                    .min(active.end_frame.unwrap_or(u64::MAX))
                    .saturating_sub(active.start_frame)
            } else {
                0
            };
            let progress = if active.overlap_frames == 0 {
                f64::from(u8::from(handed_over))
            } else {
                (consumed as f64 / active.overlap_frames as f64).min(1.0)
            };
            let phase = if !handed_over {
                TransitionPlaybackPhase::Queued
            } else if active.end_frame.is_some_and(|end| frame >= end) || active.overlap_frames == 0
            {
                TransitionPlaybackPhase::Completed
            } else if consumed < active.overlap_frames {
                TransitionPlaybackPhase::Mixing
            } else {
                TransitionPlaybackPhase::Tail
            };
            let p = active.presentation;
            let sample_rate =
                p.map_or(self.mix_format.sample_rate.max(1), |p| p.sample_rate.max(1));
            let speed = p
                .map_or(f64::from(self.speed()), |p| p.output_speed)
                .max(0.001);
            // Both source coordinates use the same existing latency-compensated
            // output clock. Decoder read-ahead is never a presentation position.
            let elapsed = if handed_over {
                ((position - active.incoming_boundary_secs).max(0.0) / speed)
                    .min(consumed as f64 / f64::from(sample_rate))
            } else {
                0.0
            };
            let outgoing = if handed_over {
                p.map(|p| {
                    (p.outgoing_start_secs + elapsed * speed * p.outgoing_tempo_ratio)
                        .min(p.outgoing_end_secs)
                })
            } else {
                Some(position)
            };
            return Some(TransitionPlaybackSnapshot {
                epoch: active.epoch,
                generation,
                request_id: active.request_id,
                outgoing_seq: active.outgoing_seq,
                incoming_seq: active.incoming_seq,
                mode: active.mode.clone(),
                phase,
                handed_over,
                progress,
                output_frames: consumed,
                overlap_frames: active.overlap_frames,
                incoming_boundary_secs: active.incoming_boundary_secs,
                incoming_position_secs: handed_over.then_some(position),
                outgoing_position_secs: outgoing,
                outgoing_end_secs: p.map(|p| p.outgoing_end_secs),
                outgoing_tempo_ratio: p.map_or(1.0, |p| p.outgoing_tempo_ratio),
                output_speed: speed,
                sample_rate,
                template: p.and_then(|p| p.template),
                presentation_cue: p.map_or(
                    TransitionPresentationCue::unavailable("no-runner-presentation-plan"),
                    |p| p.cue,
                ),
                cue_elapsed_secs: elapsed,
                cue_duration_secs: active.overlap_frames as f64 / f64::from(sample_rate),
            });
        }
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture(speed: f32) -> SharedAudio {
        let settings = DspSettings {
            speed,
            ..Default::default()
        };
        let shared = SharedAudio::new(MixFormat::stereo_f32(100), 1.0, 8.0, &settings);
        shared.set_track_seq(11);
        shared
    }

    #[test]
    fn presentation_recording_failure_does_not_block_the_existing_track_boundary() {
        let shared = fixture(1.0);
        let mut info =
            TrackSwitchInfo::new("existing-mainline-boundary.wav".into(), None, 42, 30.0);
        info.transition = Some(crate::events::TrackTransitionInfo {
            mode: "fade".into(),
            overlap_secs: 0.2,
        });
        // This deliberately invalidates only the optional presentation plan.
        // Mainline mark_track_boundary_continuous has no presentation authority.
        shared.mark_mix_boundary(
            info,
            TransitionProjectionPlan {
                request_id: 9,
                generation: shared.current_decode_generation() + 1,
                outgoing_seq: 11,
                overlap_frames: 20,
                presentation: None,
            },
        );
        assert_eq!(shared.transition_playback(), None);
        assert_eq!(
            shared
                .gapless_boundary
                .lock()
                .unwrap()
                .as_ref()
                .map(|boundary| boundary.info.seq),
            Some(42),
            "an unavailable read-only projection must not veto the existing PCM boundary"
        );
    }

    #[test]
    fn presentation_recording_failure_does_not_block_the_existing_gain_marker() {
        let shared = fixture(1.0);
        // No projection exists, so recording its end must fail. The existing
        // gain marker still belongs to the mixer, not to presentation metadata.
        shared.mark_mix_end(-3.0, 9, shared.current_decode_generation());
        assert_eq!(shared.transition_playback(), None);
        assert_eq!(
            shared
                .gain_marker
                .lock()
                .unwrap()
                .as_ref()
                .map(|marker| marker.gain_db),
            Some(-3.0),
            "an unavailable read-only projection must not veto mainline normalization"
        );
    }

    fn mark(
        shared: &SharedAudio,
        request_id: u64,
        generation: u64,
        outgoing_seq: u64,
        incoming_seq: u64,
        overlap_frames: u64,
    ) {
        let mut info =
            TrackSwitchInfo::new("real-prepared-source.wav".into(), None, incoming_seq, 90.0);
        info.start_position_secs = 7.0;
        info.transition = Some(crate::events::TrackTransitionInfo {
            mode: "automix-pro".into(),
            overlap_secs: 0.2,
        });
        shared.mark_mix_boundary(
            info,
            TransitionProjectionPlan {
                request_id,
                generation,
                outgoing_seq,
                overlap_frames,
                presentation: Some(TransitionPresentationPlan {
                    outgoing_start_secs: 100.0,
                    outgoing_end_secs: 102.0,
                    outgoing_tempo_ratio: 1.05,
                    output_speed: f64::from(shared.speed()),
                    sample_rate: shared.mix_format.sample_rate,
                    template: None,
                    cue: crate::transition::presentation_cue::gain_crossover(None, true, 1.0, 1.0),
                }),
            },
        );
    }

    #[test]
    fn consumed_mix_exposes_paired_source_positions_and_an_explicit_presentation_cue() {
        let shared = fixture(2.0);
        let (tx, _rx) = std::sync::mpsc::sync_channel(8);
        let (telemetry, _telemetry_rx) = std::sync::mpsc::sync_channel(8);
        shared.bind_signal_senders(tx, telemetry).unwrap();
        mark(&shared, 9, 0, 11, 42, 10);
        assert!(shared.push_samples(&[0.3; 20]));
        assert_eq!(shared.pop_into(&mut [0.0; 10]), 5);
        let value = serde_json::to_value(shared.transition_playback().unwrap()).unwrap();
        assert!(
            value["outgoing_position_secs"].is_number(),
            "the audible outgoing track position is missing"
        );
        assert!(
            value["presentation_cue"].is_object(),
            "absence of an envelope cue must carry an explicit readiness/reason, not linear overlap progress"
        );
        let snapshot = shared.transition_playback().unwrap();
        assert!((snapshot.outgoing_position_secs.unwrap() - 100.105).abs() < 1e-10);
        assert!((snapshot.incoming_position_secs.unwrap() - 7.1).abs() < 1e-10);
        assert!((snapshot.cue_elapsed_secs - 0.05).abs() < 1e-10);
        assert_eq!(snapshot.cue_duration_secs, 0.1);
        assert_eq!(snapshot.presentation_cue.crossover, Some(0.5));
    }

    #[test]
    fn transition_identity_json_preserves_every_u64_without_a_javascript_number() {
        let shared = fixture(1.0);
        mark(&shared, 9, 0, 11, 42, 10);
        let mut snapshot = shared.transition_playback().unwrap();
        snapshot.epoch = u64::MAX;
        snapshot.generation = u64::MAX - 1;
        snapshot.request_id = Some(9_007_199_254_740_993);
        snapshot.outgoing_seq = u64::MAX - 2;
        snapshot.incoming_seq = u64::MAX - 3;
        let value = serde_json::to_value(snapshot).unwrap();
        assert_eq!(value["epoch"], u64::MAX.to_string());
        assert_eq!(value["generation"], (u64::MAX - 1).to_string());
        assert_eq!(value["request_id"], "9007199254740993");
        assert_eq!(value["outgoing_seq"], (u64::MAX - 2).to_string());
        assert_eq!(value["incoming_seq"], (u64::MAX - 3).to_string());
    }

    #[test]
    fn independent_sessions_never_reuse_a_transition_epoch() {
        let a = fixture(1.0);
        let b = fixture(1.0);
        mark(&a, 5, 0, 11, 42, 20);
        mark(&b, 5, 0, 11, 42, 20);
        assert_ne!(
            a.transition_playback().unwrap().epoch,
            b.transition_playback().unwrap().epoch
        );
    }

    #[test]
    fn actual_release_marker_completes_after_the_nominal_overlap_and_pausing_does_not_advance() {
        let shared = fixture(1.0);
        let (tx, _rx) = std::sync::mpsc::sync_channel(8);
        let (telemetry, _telemetry_rx) = std::sync::mpsc::sync_channel(8);
        shared.bind_signal_senders(tx, telemetry).unwrap();
        assert!(shared.push_samples(&[0.2; 8])); // 4 outgoing frames.
        mark(&shared, 50, 0, 11, 42, 20);
        assert!(shared.push_samples(&[0.3; 60])); // 20 overlap + 10 tail frames.
        shared.mark_mix_end(-3.0, 50, 0);
        assert!(shared.push_samples(&[0.4; 8]));
        let queued = shared.transition_playback().unwrap();
        assert!(!queued.handed_over);
        assert_eq!(queued.incoming_position_secs, None);
        let mut output = [0.0; 48];
        assert_eq!(shared.pop_into(&mut output), 24);
        let tail = shared.transition_playback().unwrap();
        assert_eq!(tail.phase, TransitionPlaybackPhase::Tail);
        assert_eq!(tail.progress, 1.0);
        assert_eq!(tail.output_frames, 20);
        shared.paused.store(true, Ordering::Release);
        std::thread::sleep(std::time::Duration::from_millis(5));
        assert_eq!(shared.transition_playback().unwrap(), tail);
        shared.paused.store(false, Ordering::Release);
        assert_eq!(shared.pop_into(&mut output[..20]), 10);
        let complete = shared.transition_playback().unwrap();
        assert_eq!(complete.phase, TransitionPlaybackPhase::Completed);
        assert_eq!(complete.epoch, tail.epoch);
        assert_eq!(complete.output_frames, 30);
        assert_eq!(shared.pop_into(&mut output[..8]), 4);
        let dry_b = shared.transition_playback().unwrap();
        assert_eq!(
            dry_b.output_frames, complete.output_frames,
            "dry B after the real release marker is no longer mixed output"
        );
        assert!(dry_b.incoming_position_secs > complete.incoming_position_secs);
    }

    #[test]
    fn seek_stop_and_stale_request_cannot_republish_old_transition_metadata() {
        let shared = fixture(1.0);
        mark(&shared, 5, 0, 11, 42, 20);
        let first = shared.transition_playback().unwrap().epoch;
        let generation = shared.reset_for_decode_resume(9.0, &DspSettings::default());
        assert_eq!(shared.transition_playback(), None);
        mark(&shared, 5, 0, 11, 42, 20);
        assert_eq!(shared.transition_playback(), None);
        assert_eq!(
            shared
                .gapless_boundary
                .lock()
                .unwrap()
                .as_ref()
                .map(|boundary| boundary.info.seq),
            Some(42),
            "presentation invalidation must not add a new veto to the mainline boundary"
        );
        mark(&shared, 6, generation, 11, 99, 20);
        let current = shared.transition_playback().unwrap();
        assert_ne!(current.epoch, first);
        shared.mark_mix_end(-3.0, 5, generation);
        assert_eq!(
            shared
                .gain_marker
                .lock()
                .unwrap()
                .as_ref()
                .map(|marker| marker.gain_db),
            Some(-3.0)
        );
        shared.mark_mix_end(-4.0, 6, 0);
        assert_eq!(
            shared
                .gain_marker
                .lock()
                .unwrap()
                .as_ref()
                .map(|marker| marker.gain_db),
            Some(-4.0)
        );
        assert_eq!(shared.transition_playback().unwrap(), current);
        shared.request_stop();
        assert_eq!(shared.transition_playback(), None);
        mark(&shared, 6, generation, 11, 99, 20);
        assert_eq!(shared.transition_playback(), None);
    }

    #[test]
    fn progress_counts_real_output_frames_at_non_unit_speed_and_does_not_follow_read_ahead() {
        let shared = fixture(2.0);
        mark(&shared, 9, 0, 11, 42, 10); // 0.2 source seconds / 2x = 10 output frames.
        assert!(shared.push_samples(&[0.3; 20]));
        assert_eq!(shared.transition_playback().unwrap().progress, 0.0);
        let mut output = [0.0; 10];
        assert_eq!(shared.pop_into(&mut output), 5);
        let current = shared.transition_playback().unwrap();
        assert_eq!(current.progress, 0.5);
        assert_eq!(current.output_frames, 5);
        assert_eq!(current.incoming_position_secs, Some(shared.position_secs()));
        assert_eq!(current.incoming_position_secs, Some(7.1));
        assert_eq!(shared.pop_into(&mut output), 5);
        let before_underrun = shared.transition_playback().unwrap();
        assert_eq!(shared.pop_into(&mut output), 0);
        assert_eq!(shared.transition_playback().unwrap(), before_underrun);
    }
}
