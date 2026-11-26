//! Plain Rust work contracts. The host runs blocking decoder/DSP work on workers.
//! Construct work on command submission so source/seek generation guards apply
//! before a queued command runs. No runtime, JavaScript or GUI dependency lives here.
#[derive(Debug, Clone)]
pub struct EngineError {
    pub reason: String,
    pub output: bool,
    pub cancelled: bool,
}
impl EngineError {
    pub fn from_reason(reason: impl Into<String>) -> Self {
        Self {
            reason: reason.into(),
            output: false,
            cancelled: false,
        }
    }
    pub fn from_output_reason(reason: impl Into<String>) -> Self {
        Self {
            output: true,
            ..Self::from_reason(reason)
        }
    }
    pub(crate) fn output_start_cancelled() -> Self {
        Self {
            cancelled: true,
            ..Self::from_output_reason("audio output startup was cancelled")
        }
    }
}
impl std::fmt::Display for EngineError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.reason)
    }
}
impl std::error::Error for EngineError {}
pub type Result<T, E = EngineError> = std::result::Result<T, E>;
pub trait Command: Send {
    type Output;
    fn compute(&mut self) -> Result<Self::Output>;
}
pub struct Work<T: Command>(T);
impl<T: Command> Work<T> {
    pub fn new(command: T) -> Self {
        Self(command)
    }
    pub fn execute(mut self) -> Result<T::Output> {
        self.0.compute()
    }
}
