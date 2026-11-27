mod file;
mod http;
pub mod sniff;
mod url;

use std::io::{Read, Seek};
use std::sync::Arc;
use std::sync::atomic::AtomicBool;
use std::time::Duration;

pub trait ReadSeek: Read + Seek + Send {}

impl<T> ReadSeek for T where T: Read + Seek + Send {}

#[derive(Clone, Debug)]
pub struct StreamOptions {
    pub network_timeout: Duration,
    pub http_proxies: Vec<Option<String>>,
    pub user_agent: String,
}

impl Default for StreamOptions {
    fn default() -> Self {
        Self {
            network_timeout: Duration::from_secs(60),
            http_proxies: vec![None],
            user_agent: String::new(),
        }
    }
}

pub fn open_stream(
    url: &str,
    interrupt: Arc<AtomicBool>,
    options: &StreamOptions,
) -> Result<Box<dyn ReadSeek>, String> {
    if is_network_url(url) {
        return http::open(url, interrupt, options);
    }
    file::open(url)
}

pub fn is_network_url(url: &str) -> bool {
    http::is_http_url(url)
}

/// Retain the route actually selected after proxy transport fallback so nested
/// resources do not silently switch back to a failed proxy or direct access.
pub fn open_decoder_input(
    url: &str,
    interrupt: Arc<AtomicBool>,
    options: &StreamOptions,
) -> Result<(Box<dyn ReadSeek>, StreamOptions), String> {
    if is_network_url(url) {
        let (source, proxy) = http::open_with_route(url, interrupt, options)?;
        let mut selected = options.clone();
        selected.http_proxies = vec![proxy];
        Ok((source, selected))
    } else {
        Ok((file::open(url)?, options.clone()))
    }
}
