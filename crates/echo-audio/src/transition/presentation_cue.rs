//! Presentation uses the *selected* native gain automation, never an assumed
//! midpoint or acoustic/RMS measurement. EQ, echoes and parameter smoothing do
//! not define an additional imaginary crossover. Work is bounded at setup.
use super::plan::{DjPlan, EffectChainSpec, EffectTarget};
use serde::Serialize;

#[derive(Clone, Copy, Debug, PartialEq, Serialize)]
pub struct TransitionPresentationCue {
    pub ready: bool,
    pub crossover: Option<f64>,
    pub reason: &'static str,
}

impl TransitionPresentationCue {
    pub const fn unavailable(reason: &'static str) -> Self {
        Self {
            ready: false,
            crossover: None,
            reason,
        }
    }
    fn ready(crossover: f64) -> Self {
        Self {
            ready: true,
            crossover: Some(crossover),
            reason: "scheduled-gain-envelope",
        }
    }
}

fn gain(chain: &EffectChainSpec, position: f32) -> Option<f64> {
    let mut result = 1.0;
    let mut count = 0;
    for slot in &chain.list {
        if slot.target() == EffectTarget::GainDb {
            count += 1;
            if count > 32 {
                return None;
            }
            // The exact evaluator also used by StageKind::Gain in effects.rs.
            result *= f64::from(slot.automation.evaluate_gain_linear(position));
        }
    }
    (count > 0 && result.is_finite()).then_some(result)
}

pub fn gain_crossover(
    plan: Option<&DjPlan>,
    linear_fade: bool,
    a_gain: f32,
    b_gain: f32,
) -> TransitionPresentationCue {
    let trim = |value: f32| {
        if value.is_finite() {
            f64::from(value.clamp(0.0, 16.0))
        } else {
            1.0
        }
    };
    let (a, b) = (trim(a_gain), trim(b_gain));
    if a <= 0.0 || b <= 0.0 {
        return TransitionPresentationCue::unavailable("silent-deck-gain");
    }
    if linear_fade {
        return TransitionPresentationCue::ready(a / (a + b));
    }
    let Some(plan) = plan else {
        return TransitionPresentationCue::unavailable("no-scheduled-gain-envelope");
    };
    let difference = |position: f32| {
        Some(gain(&plan.a_chain, position)? * a - gain(&plan.b_chain, position)? * b)
    };
    let Some(first) = difference(0.0) else {
        // In particular EchoTail has an echo on A, not a reciprocal gain ramp.
        return TransitionPresentationCue::unavailable("missing-paired-gain-envelope");
    };
    let Some(last) = difference(1.0) else {
        return TransitionPresentationCue::unavailable("invalid-gain-envelope");
    };
    if first <= 0.0 || last >= 0.0 {
        return TransitionPresentationCue::unavailable("no-interior-gain-crossing");
    }
    const STEPS: usize = 4096;
    let mut before = first;
    let mut bracket = None;
    for index in 1..=STEPS {
        let position = index as f32 / STEPS as f32;
        let Some(value) = difference(position) else {
            return TransitionPresentationCue::unavailable("invalid-gain-envelope");
        };
        if before > 0.0 && value <= 0.0 {
            if bracket.is_some() {
                return TransitionPresentationCue::unavailable("multiple-gain-crossings");
            }
            bracket = Some(((index - 1) as f32 / STEPS as f32, position));
        } else if before <= 0.0 && value > 0.0 {
            return TransitionPresentationCue::unavailable("multiple-gain-crossings");
        }
        before = value;
    }
    let Some((mut low, mut high)) = bracket else {
        return TransitionPresentationCue::unavailable("no-interior-gain-crossing");
    };
    for _ in 0..32 {
        let middle = (low + high) * 0.5;
        if difference(middle).unwrap_or(f64::NAN) > 0.0 {
            low = middle;
        } else {
            high = middle;
        }
    }
    TransitionPresentationCue::ready(f64::from(high))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::transition::plan::PlanTemplate;
    #[test]
    fn selected_templates_use_the_native_gain_evaluator_and_never_invent_echo_tail_cues() {
        for template in [
            PlanTemplate::SimpleFilter,
            PlanTemplate::ThreeBand,
            PlanTemplate::SimpleExchange,
            PlanTemplate::FilterEq,
            PlanTemplate::FallbackExchange,
            PlanTemplate::SmoothFilter,
            PlanTemplate::LayeredFilter,
        ] {
            let plan = template.load().unwrap();
            let cue = gain_crossover(Some(&plan), false, 1.0, 1.0);
            assert!(cue.ready, "{}: {:?}", template.id(), cue);
            let at = cue.crossover.unwrap() as f32;
            assert!(
                (gain(&plan.a_chain, at).unwrap() - gain(&plan.b_chain, at).unwrap()).abs() < 1e-5
            );
            if template == PlanTemplate::LayeredFilter {
                assert!(at > 0.8);
            }
        }
        let tail = PlanTemplate::EchoTail.load().unwrap();
        assert_eq!(
            gain_crossover(Some(&tail), false, 1.0, 1.0),
            TransitionPresentationCue::unavailable("missing-paired-gain-envelope")
        );
        assert_eq!(
            gain_crossover(None, true, 1.0, 2.0).crossover,
            Some(1.0 / 3.0)
        );
    }
}
