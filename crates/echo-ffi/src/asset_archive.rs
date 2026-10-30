//! Generic image archive transport. The caller is a killable host helper, never QML.

use serde_json::{Value, json};
use std::{
    collections::HashSet,
    ffi::CStr,
    fs,
    io::{Read, Seek, SeekFrom, Write},
    panic::{AssertUnwindSafe, catch_unwind},
    path::Path,
};

const MIB: u64 = 1024 * 1024;
const ZIP_LIMIT: u64 = 72 * MIB;
const EXPANDED_LIMIT: u64 = 64 * MIB + 256 * 1024;
const PROTOCOL_LIMIT: u64 = 2 * MIB;

fn string<'a>(value: &'a Value, key: &str) -> Result<&'a str, String> {
    value
        .get(key)
        .and_then(Value::as_str)
        .filter(|value| !value.is_empty())
        .ok_or_else(|| format!("Missing archive {key}"))
}
fn regular(path: &Path, maximum: u64) -> Result<u64, String> {
    let info = fs::symlink_metadata(path).map_err(|error| error.to_string())?;
    if !info.is_file() || info.len() > maximum {
        return Err("Archive file type or byte budget exceeded".into());
    }
    Ok(info.len())
}
fn safe_name(name: &str, directory: bool) -> Result<(), String> {
    let name = if directory {
        name.strip_suffix('/').unwrap_or(name)
    } else {
        name
    };
    if name.is_empty()
        || name.len() > 2048
        || name.starts_with('/')
        || name.contains(['\\', ':'])
        || name.chars().any(char::is_control)
        || name
            .split('/')
            .any(|part| part.is_empty() || part == "." || part == "..")
    {
        return Err("Unsafe archive entry name".into());
    }
    Ok(())
}
fn read_bounded(reader: impl Read, expected: u64, maximum: u64) -> Result<Vec<u8>, String> {
    if expected > maximum {
        return Err("Archive entry byte budget exceeded".into());
    }
    let mut output = Vec::new();
    reader
        .take(expected + 1)
        .read_to_end(&mut output)
        .map_err(|error| error.to_string())?;
    if output.len() as u64 != expected {
        return Err("Archive entry size mismatch".into());
    }
    Ok(output)
}
fn json_depth(value: &Value, depth: usize) -> bool {
    depth <= 64
        && match value {
            Value::Array(values) => values.iter().all(|value| json_depth(value, depth + 1)),
            Value::Object(values) => values.values().all(|value| json_depth(value, depth + 1)),
            _ => true,
        }
}
fn sniff(bytes: &[u8]) -> &'static str {
    if bytes.starts_with(b"\x89PNG\r\n\x1a\n") {
        return "image/png";
    }
    if bytes.starts_with(b"\xff\xd8\xff") {
        return "image/jpeg";
    }
    if bytes.starts_with(b"GIF87a") || bytes.starts_with(b"GIF89a") {
        return "image/gif";
    }
    if bytes.starts_with(b"RIFF") && bytes.get(8..12) == Some(b"WEBP") {
        return "image/webp";
    }
    if let Ok(text) = std::str::from_utf8(bytes) {
        let mut text = text.trim_start_matches('\u{feff}').trim_start();
        if text.starts_with("<?xml") {
            if let Some(end) = text.find("?>") {
                text = text[end + 2..].trim_start();
            }
        }
        if text.starts_with("<svg")
            && text[4..].starts_with(|c: char| c.is_whitespace() || c == '>' || c == '/')
        {
            return "image/svg+xml";
        }
    }
    ""
}
struct Entry {
    index: usize,
    name: String,
    size: u64,
    directory: bool,
}
fn inventory(zip: &mut zip::ZipArchive<MetadataBoundedFile>) -> Result<Vec<Entry>, String> {
    if zip.len() > 256 {
        return Err("Archive exceeds 256 entries".into());
    }
    let mut entries = Vec::new();
    let mut names = HashSet::new();
    let mut expanded = 0u64;
    for index in 0..zip.len() {
        let entry = zip.by_index(index).map_err(|error| error.to_string())?;
        let name = std::str::from_utf8(entry.name_raw())
            .map_err(|_| "Archive names must be UTF-8")?
            .to_owned();
        safe_name(&name, entry.is_dir())?;
        if !names.insert(name.trim_end_matches('/').to_lowercase()) {
            return Err("Duplicate archive path".into());
        }
        let mode = entry.unix_mode().unwrap_or(0) & 0o170000;
        if mode != 0 && mode != 0o100000 && mode != 0o040000 {
            return Err("Archive links and special files are forbidden".into());
        }
        if entry.encrypted()
            || !matches!(
                entry.compression(),
                zip::CompressionMethod::Stored | zip::CompressionMethod::Deflated
            )
        {
            return Err("Encrypted or unsupported archive compression".into());
        }
        expanded = expanded
            .checked_add(entry.size())
            .ok_or("Archive expanded size overflow")?;
        if expanded > EXPANDED_LIMIT {
            return Err("Archive expanded byte budget exceeded".into());
        }
        entries.push(Entry {
            index,
            name,
            size: entry.size(),
            directory: entry.is_dir(),
        });
    }
    Ok(entries)
}
// Parse only a bounded end record and raw central records before zip allocates
// its metadata Vec or folds duplicate names into an IndexMap.
fn le16(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes(bytes[offset..offset + 2].try_into().unwrap())
}
fn le32(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes(bytes[offset..offset + 4].try_into().unwrap())
}
fn le64(bytes: &[u8], offset: usize) -> u64 {
    u64::from_le_bytes(bytes[offset..offset + 8].try_into().unwrap())
}
fn at<const N: usize>(file: &mut fs::File, offset: u64) -> Result<[u8; N], String> {
    file.seek(SeekFrom::Start(offset))
        .map_err(|error| error.to_string())?;
    let mut bytes = [0; N];
    file.read_exact(&mut bytes)
        .map_err(|error| error.to_string())?;
    Ok(bytes)
}
fn directory_preflight(file: &mut fs::File, length: u64) -> Result<(u64, usize), String> {
    if length < 22 {
        return Err("ZIP end record is missing".into());
    }
    let tail_length = length.min(65535 + 22);
    file.seek(SeekFrom::Start(length - tail_length))
        .map_err(|error| error.to_string())?;
    let mut tail = vec![0; tail_length as usize];
    file.read_exact(&mut tail)
        .map_err(|error| error.to_string())?;
    let end = (0..=tail.len() - 22)
        .rev()
        .find(|&offset| {
            tail[offset..offset + 4] == *b"PK\x05\x06"
                && offset + 22 + le16(&tail, offset + 20) as usize == tail.len()
        })
        .ok_or("ZIP must have one terminal end record without trailing data")?;
    let eocd = &tail[end..end + 22];
    let eocd_offset = length - tail_length + end as u64;
    if le16(eocd, 4) != 0 || le16(eocd, 6) != 0 {
        return Err("Multi-disk ZIP is forbidden".into());
    }
    let mut count = u64::from(le16(eocd, 10));
    let mut size = u64::from(le32(eocd, 12));
    let mut start = u64::from(le32(eocd, 16));
    let mut directory_end = eocd_offset;
    let mut zip64_offset = None;
    if count == 65535
        || le16(eocd, 8) == 65535
        || size == u32::MAX as u64
        || start == u32::MAX as u64
    {
        let locator_offset = eocd_offset
            .checked_sub(20)
            .ok_or("ZIP64 locator is missing")?;
        let locator = at::<20>(file, locator_offset)?;
        if locator[..4] != *b"PK\x06\x07" || le32(&locator, 4) != 0 || le32(&locator, 16) != 1 {
            return Err("Invalid or multi-disk ZIP64 locator".into());
        }
        let record_offset = le64(&locator, 8);
        if record_offset
            .checked_add(56)
            .is_none_or(|end| end > locator_offset)
        {
            return Err("ZIP64 end record is outside its bounded extent".into());
        }
        let record = at::<56>(file, record_offset)?;
        let record_size = le64(&record, 4);
        if record[..4] != *b"PK\x06\x06"
            || !(44..=65536).contains(&record_size)
            || record_offset
                .checked_add(12)
                .and_then(|value| value.checked_add(record_size))
                != Some(locator_offset)
            || le32(&record, 16) != 0
            || le32(&record, 20) != 0
            || le64(&record, 24) != le64(&record, 32)
        {
            return Err("Invalid or oversized ZIP64 end record".into());
        }
        count = le64(&record, 32);
        size = le64(&record, 40);
        start = le64(&record, 48);
        directory_end = record_offset;
        zip64_offset = Some(record_offset);
        for (small, large, sentinel) in [
            (u64::from(le16(eocd, 8)), count, 65535),
            (u64::from(le16(eocd, 10)), count, 65535),
            (u64::from(le32(eocd, 12)), size, u32::MAX as u64),
            (u64::from(le32(eocd, 16)), start, u32::MAX as u64),
        ] {
            if small != sentinel && small != large {
                return Err("ZIP32/ZIP64 metadata disagree".into());
            }
        }
    } else if le16(eocd, 8) != le16(eocd, 10) {
        return Err("ZIP entry counts disagree".into());
    }
    if count > 256 {
        return Err("Archive header exceeds 256 raw entries".into());
    }
    if start.checked_add(size) != Some(directory_end) || directory_end > length {
        return Err("ZIP central directory bounds are inconsistent".into());
    }
    let mut cursor = start;
    let mut names = HashSet::new();
    for _ in 0..count {
        if cursor.checked_add(46).is_none_or(|end| end > directory_end) {
            return Err("Truncated ZIP central record".into());
        }
        let header = at::<46>(file, cursor)?;
        if header[..4] != *b"PK\x01\x02" {
            return Err("ZIP raw central record count mismatch".into());
        }
        let name_size = le16(&header, 28) as usize;
        let next = cursor
            + 46
            + name_size as u64
            + u64::from(le16(&header, 30))
            + u64::from(le16(&header, 32));
        if name_size == 0 || name_size > 2048 || next > directory_end {
            return Err("ZIP raw central record exceeds its name or extent budget".into());
        }
        let mut name = vec![0; name_size];
        file.read_exact(&mut name)
            .map_err(|error| error.to_string())?;
        let name = std::str::from_utf8(&name).map_err(|_| "Archive names must be UTF-8")?;
        safe_name(name, name.ends_with('/'))?;
        if !names.insert(name.trim_end_matches('/').to_lowercase()) {
            return Err("Duplicate raw archive path".into());
        }
        cursor = next;
    }
    if cursor != directory_end {
        return Err("ZIP raw central directory size mismatch".into());
    }
    // zip retries earlier EOCDs if parsing a candidate fails. It must not see
    // unvalidated end records in payload, nor ambiguous end markers inside
    // central metadata/comments. Payload bytes become visible again after new().
    let mut position = start;
    let mut overlap = Vec::new();
    while position < length {
        let amount = (length - position).min(65536) as usize;
        file.seek(SeekFrom::Start(position))
            .map_err(|error| error.to_string())?;
        let mut chunk = vec![0; amount];
        file.read_exact(&mut chunk)
            .map_err(|error| error.to_string())?;
        let overlap_size = overlap.len();
        overlap.extend(chunk);
        for index in 0..overlap.len().saturating_sub(3) {
            let absolute = position - overlap_size as u64 + index as u64;
            if (&overlap[index..index + 4] == b"PK\x05\x06" && absolute != eocd_offset)
                || (&overlap[index..index + 4] == b"PK\x06\x06" && Some(absolute) != zip64_offset)
            {
                return Err("Ambiguous ZIP end marker in central metadata".into());
            }
        }
        overlap = overlap[overlap.len().saturating_sub(3)..].to_vec();
        position += amount as u64;
    }
    Ok((start, count as usize))
}
struct MetadataBoundedFile {
    file: fs::File,
    central_start: u64,
    metadata_only: std::sync::Arc<std::sync::atomic::AtomicBool>,
}
impl Seek for MetadataBoundedFile {
    fn seek(&mut self, position: SeekFrom) -> std::io::Result<u64> {
        self.file.seek(position)
    }
}
impl Read for MetadataBoundedFile {
    fn read(&mut self, buffer: &mut [u8]) -> std::io::Result<usize> {
        use std::sync::atomic::Ordering;
        let position = self.file.stream_position()?;
        if self.metadata_only.load(Ordering::Relaxed) && position < self.central_start {
            let zeros = (self.central_start - position).min(buffer.len() as u64) as usize;
            buffer[..zeros].fill(0);
            self.file.seek(SeekFrom::Current(zeros as i64))?;
            if zeros == buffer.len() {
                return Ok(zeros);
            }
            return self
                .file
                .read(&mut buffer[zeros..])
                .map(|read| zeros + read);
        }
        self.file.read(buffer)
    }
}
fn open(request: &Value) -> Result<zip::ZipArchive<MetadataBoundedFile>, String> {
    use std::sync::{
        Arc,
        atomic::{AtomicBool, Ordering},
    };
    let path = Path::new(string(request, "archive")?);
    let length = regular(path, ZIP_LIMIT)?;
    let mut file = fs::File::open(path).map_err(|error| error.to_string())?;
    let (central_start, count) = directory_preflight(&mut file, length)?;
    file.rewind().map_err(|error| error.to_string())?;
    let metadata_only = Arc::new(AtomicBool::new(true));
    let result = zip::ZipArchive::new(MetadataBoundedFile {
        file,
        central_start,
        metadata_only: metadata_only.clone(),
    })
    .map_err(|error| error.to_string())?;
    metadata_only.store(false, Ordering::Relaxed);
    if result.len() != count {
        return Err("ZIP metadata entry count changed after preflight".into());
    }
    Ok(result)
}
fn inspect(request: &Value) -> Result<Value, String> {
    let mut zip = open(request)?;
    let entries = inventory(&mut zip)?;
    let paths = request["jsonPaths"]
        .as_array()
        .ok_or("Missing JSON paths")?;
    if paths.len() > 4 {
        return Err("Archive exceeds four JSON files".into());
    }
    let mut metadata = serde_json::Map::new();
    let mut requested = HashSet::new();
    for path in paths {
        let path = path.as_str().ok_or("Invalid JSON path")?;
        safe_name(path, false)?;
        if !path.ends_with(".json") || !requested.insert(path) {
            return Err("Invalid or duplicate JSON path".into());
        }
        let entry = entries
            .iter()
            .find(|entry| entry.name == path && !entry.directory)
            .ok_or("Archive JSON file is missing")?;
        let reader = zip
            .by_index(entry.index)
            .map_err(|error| error.to_string())?;
        let bytes = read_bounded(reader, entry.size, 65536)?;
        let value: Value = serde_json::from_slice(&bytes).map_err(|error| error.to_string())?;
        if !json_depth(&value, 0) {
            return Err("Archive JSON exceeds depth 64".into());
        }
        metadata.insert(path.to_owned(), value);
    }
    let mut output = Vec::new();
    for entry in entries.iter().filter(|entry| !entry.directory) {
        let mut reader = zip
            .by_index(entry.index)
            .map_err(|error| error.to_string())?;
        let mut header = Vec::new();
        (&mut reader)
            .take(2048)
            .read_to_end(&mut header)
            .map_err(|error| error.to_string())?;
        output.push(json!({"path":entry.name, "bytes":entry.size, "mimeType":sniff(&header)}));
    }
    Ok(
        json!({"entries":output, "jsonFiles":metadata, "rawPaths":entries.iter().map(|entry| entry.name.clone()).collect::<Vec<_>>()}),
    )
}
fn extract(request: &Value) -> Result<Value, String> {
    let mut zip = open(request)?;
    let inventory = inventory(&mut zip)?;
    let selected = request["entries"]
        .as_array()
        .ok_or("Missing selected archive entries")?;
    if selected.is_empty() || selected.len() > 16 {
        return Err("Select 1..16 archive images".into());
    }
    let mut total = 0u64;
    for item in selected {
        let name = string(item, "path")?;
        let entry = inventory
            .iter()
            .find(|entry| entry.name == name && !entry.directory)
            .ok_or("Archive image is missing")?;
        if entry.size == 0 || entry.size > 8 * MIB {
            return Err("Archive image exceeds 8 MiB".into());
        }
        total = total
            .checked_add(entry.size)
            .ok_or("Archive size overflow")?;
        if total > 64 * MIB {
            return Err("Selected archive images exceed 64 MiB".into());
        }
        // Targets are host-generated private names from the trusted helper request.
        // ZIP names never participate in a filesystem join.
        let target = Path::new(string(item, "target")?);
        let mut file = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(target)
            .map_err(|error| error.to_string())?;
        let mut reader = zip
            .by_index(entry.index)
            .map_err(|error| error.to_string())?;
        let mut count = 0u64;
        let mut buffer = [0u8; 65536];
        loop {
            let size = reader
                .read(&mut buffer)
                .map_err(|error| error.to_string())?;
            if size == 0 {
                break;
            }
            count += size as u64;
            if count > entry.size {
                return Err("Archive actual size exceeds declaration".into());
            }
            file.write_all(&buffer[..size])
                .map_err(|error| error.to_string())?;
        }
        if count != entry.size {
            return Err("Archive actual size does not match declaration".into());
        }
        file.flush().map_err(|error| error.to_string())?;
    }
    Ok(json!({"extracted":selected.len(), "bytes":total}))
}
struct LimitedFile {
    file: fs::File,
    position: u64,
}
impl Write for LimitedFile {
    fn write(&mut self, bytes: &[u8]) -> std::io::Result<usize> {
        if self
            .position
            .checked_add(bytes.len() as u64)
            .is_none_or(|end| end > ZIP_LIMIT)
        {
            return Err(std::io::Error::other("ZIP output exceeds 72 MiB"));
        }
        let count = self.file.write(bytes)?;
        self.position += count as u64;
        Ok(count)
    }
    fn flush(&mut self) -> std::io::Result<()> {
        self.file.flush()
    }
}
impl Seek for LimitedFile {
    fn seek(&mut self, from: SeekFrom) -> std::io::Result<u64> {
        let target = match from {
            SeekFrom::Start(value) => i128::from(value),
            SeekFrom::Current(value) => i128::from(self.position) + i128::from(value),
            SeekFrom::End(value) => i128::from(self.file.metadata()?.len()) + i128::from(value),
        };
        if target < 0 || target > i128::from(ZIP_LIMIT) {
            return Err(std::io::Error::other("ZIP seek exceeds output budget"));
        }
        self.position = self.file.seek(SeekFrom::Start(target as u64))?;
        Ok(self.position)
    }
}
fn export(request: &Value) -> Result<Value, String> {
    let files = request["files"]
        .as_array()
        .ok_or("Missing archive export files")?;
    if files.is_empty() || files.len() > 256 {
        return Err("Archive export exceeds its entry budget".into());
    }
    let mut names = HashSet::new();
    let mut total = 0u64;
    for entry in files {
        let name = string(entry, "path")?;
        safe_name(name, false)?;
        if !names.insert(name.to_lowercase()) {
            return Err("Duplicate archive export path".into());
        }
        total = total
            .checked_add(regular(Path::new(string(entry, "source")?), 8 * MIB)?)
            .ok_or("Archive size overflow")?;
        if total > EXPANDED_LIMIT {
            return Err("Archive export expanded bytes exceed budget".into());
        }
    }
    let output = Path::new(string(request, "output")?);
    let file = fs::OpenOptions::new()
        .write(true)
        .read(true)
        .create_new(true)
        .open(output)
        .map_err(|error| error.to_string())?;
    let mut writer = zip::ZipWriter::new(LimitedFile { file, position: 0 });
    for entry in files {
        let path = Path::new(string(entry, "source")?);
        let expected = regular(path, 8 * MIB)?;
        writer
            .start_file(
                string(entry, "path")?,
                zip::write::SimpleFileOptions::default()
                    .compression_method(zip::CompressionMethod::Deflated)
                    .compression_level(Some(6))
                    .unix_permissions(0o644),
            )
            .map_err(|error| error.to_string())?;
        let mut source = fs::File::open(path).map_err(|error| error.to_string())?;
        let mut copied = 0u64;
        let mut buffer = [0u8; 65536];
        loop {
            let count = source
                .read(&mut buffer)
                .map_err(|error| error.to_string())?;
            if count == 0 {
                break;
            }
            copied += count as u64;
            if copied > expected {
                return Err("Archive source changed during export".into());
            }
            writer
                .write_all(&buffer[..count])
                .map_err(|error| error.to_string())?;
        }
        if copied != expected {
            return Err("Archive source changed during export".into());
        }
    }
    let mut output = writer.finish().map_err(|error| error.to_string())?;
    output.flush().map_err(|error| error.to_string())?;
    let bytes = output
        .file
        .metadata()
        .map_err(|error| error.to_string())?
        .len();
    Ok(json!({"exported":files.len(), "bytes":bytes}))
}
fn run(request: &Value) -> Result<Value, String> {
    match string(request, "command")? {
        "inspect" => inspect(request),
        "extract" => extract(request),
        "export" => export(request),
        _ => Err("Unknown asset archive command".into()),
    }
}

/// Worker-process only bounded archive transport; all paths are host-generated.
/// # Safety
/// Paths must be NUL-terminated; error must address error_capacity writable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_asset_archive(
    request: *const std::ffi::c_char,
    output: *const std::ffi::c_char,
    error: *mut u8,
    error_capacity: usize,
) -> bool {
    let result = catch_unwind(AssertUnwindSafe(|| {
        if request.is_null() || output.is_null() {
            return Err("Missing archive protocol path".to_owned());
        }
        let request = unsafe { CStr::from_ptr(request) }
            .to_str()
            .map_err(|error| error.to_string())?;
        let output = unsafe { CStr::from_ptr(output) }
            .to_str()
            .map_err(|error| error.to_string())?;
        let path = Path::new(request);
        let length = regular(path, PROTOCOL_LIMIT)?;
        let bytes = read_bounded(
            fs::File::open(path).map_err(|error| error.to_string())?,
            length,
            PROTOCOL_LIMIT,
        )?;
        let request: Value = serde_json::from_slice(&bytes).map_err(|error| error.to_string())?;
        if !json_depth(&request, 0) {
            return Err("Archive request nesting exceeds 64".into());
        }
        let result = serde_json::to_vec(&run(&request)?).map_err(|error| error.to_string())?;
        if result.len() as u64 > PROTOCOL_LIMIT {
            return Err("Archive metadata exceeds 2 MiB".into());
        }
        fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(output)
            .map_err(|error| error.to_string())?
            .write_all(&result)
            .map_err(|error| error.to_string())
    }))
    .unwrap_or_else(|_| Err("Asset archive panic".into()));
    match result {
        Ok(()) => true,
        Err(message) => {
            if !error.is_null() && error_capacity > 0 {
                let length = message.len().min(error_capacity - 1);
                unsafe {
                    std::ptr::copy_nonoverlapping(message.as_ptr(), error, length);
                    *error.add(length) = 0;
                }
            }
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::{
        fs,
        io::{Read, Write},
        path::PathBuf,
        sync::atomic::{AtomicU64, Ordering},
    };
    use zip::{ZipArchive, ZipWriter, write::SimpleFileOptions};

    static NEXT: AtomicU64 = AtomicU64::new(0);
    struct Fixture(PathBuf);
    impl Fixture {
        fn new() -> Self {
            let path = std::env::temp_dir().join(format!(
                "echo-asset-archive-{}-{}",
                std::process::id(),
                NEXT.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir_all(&path).unwrap();
            Self(path)
        }
        fn archive(&self, names: &[(&str, &[u8])]) -> PathBuf {
            let path = self.0.join("input.zip");
            let mut writer = ZipWriter::new(fs::File::create(&path).unwrap());
            for (name, bytes) in names {
                writer
                    .start_file(
                        *name,
                        SimpleFileOptions::default()
                            .compression_method(zip::CompressionMethod::Deflated),
                    )
                    .unwrap();
                writer.write_all(bytes).unwrap();
            }
            writer.finish().unwrap();
            path
        }
    }
    impl Drop for Fixture {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }

    #[test]
    fn directory_count_is_bounded_before_zip_metadata_allocation() {
        let fixture = Fixture::new();
        let names: Vec<String> = (0..257).map(|index| format!("images/{index}")).collect();
        let input: Vec<(&str, &[u8])> =
            names.iter().map(|name| (name.as_str(), &b""[..])).collect();
        let archive = fixture.archive(&input);
        assert!(
            open(&json!({"archive":archive})).is_err(),
            "257 raw records must be refused before ZipArchive construction"
        );
    }

    #[test]
    fn exact_duplicate_raw_records_are_not_hidden_by_zip_indexmap() {
        let fixture = Fixture::new();
        let archive = fixture.archive(&[("images/a", b"one"), ("images/b", b"two")]);
        let mut bytes = fs::read(&archive).unwrap();
        for index in 0..bytes.len() - 7 {
            if &bytes[index..index + 8] == b"images/b" {
                bytes[index + 7] = b'a';
            }
        }
        fs::write(&archive, bytes).unwrap();
        assert!(
            run(&json!({"command":"inspect", "archive":archive, "jsonPaths":[]})).is_err(),
            "raw identical names must be rejected before library deduplication"
        );
    }

    #[test]
    fn zip64_count_is_checked_without_library_metadata_allocation() {
        let fixture = Fixture::new();
        let archive = fixture.archive(&[("images/a", b"GIF89a")]);
        let bytes = fs::read(&archive).unwrap();
        let end = bytes.len() - 22;
        let mut eocd = bytes[end..].to_vec();
        let mut zip64 = bytes[..end].to_vec();
        zip64.extend(b"PK\x06\x06");
        zip64.extend(44u64.to_le_bytes());
        zip64.extend(45u16.to_le_bytes());
        zip64.extend(45u16.to_le_bytes());
        zip64.extend(0u32.to_le_bytes());
        zip64.extend(0u32.to_le_bytes());
        zip64.extend(1u64.to_le_bytes());
        zip64.extend(1u64.to_le_bytes());
        zip64.extend(u64::from(le32(&eocd, 12)).to_le_bytes());
        zip64.extend(u64::from(le32(&eocd, 16)).to_le_bytes());
        zip64.extend(b"PK\x06\x07");
        zip64.extend(0u32.to_le_bytes());
        zip64.extend((end as u64).to_le_bytes());
        zip64.extend(1u32.to_le_bytes());
        eocd[8..12].fill(255);
        eocd[12..20].fill(255);
        zip64.extend(&eocd);
        fs::write(&archive, &zip64).unwrap();
        assert!(
            open(&json!({"archive":archive})).is_ok(),
            "ordinary bounded ZIP64 must remain readable"
        );
        zip64[end + 24..end + 32].copy_from_slice(&257u64.to_le_bytes());
        zip64[end + 32..end + 40].copy_from_slice(&257u64.to_le_bytes());
        fs::write(&archive, &zip64).unwrap();
        let error = match open(&json!({"archive":archive})) {
            Ok(_) => panic!("ZIP64 count escaped preflight"),
            Err(error) => error,
        };
        assert!(error.contains("256 raw entries"), "{error}");
    }

    #[test]
    fn metadata_mask_never_changes_original_payload_bytes() {
        use std::sync::{
            Arc,
            atomic::{AtomicBool, Ordering},
        };
        let fixture = Fixture::new();
        let path = fixture.0.join("payload");
        let original = b"GIF89a-PK\x05\x06-PK\x06\x06-original";
        fs::write(&path, original).unwrap();
        let state = Arc::new(AtomicBool::new(true));
        let mut reader = MetadataBoundedFile {
            file: fs::File::open(path).unwrap(),
            central_start: original.len() as u64,
            metadata_only: state.clone(),
        };
        let mut bytes = vec![0; original.len()];
        reader.read_exact(&mut bytes).unwrap();
        assert!(bytes.iter().all(|byte| *byte == 0));
        state.store(false, Ordering::Relaxed);
        reader.rewind().unwrap();
        reader.read_exact(&mut bytes).unwrap();
        assert_eq!(bytes, original);
    }

    #[test]
    fn inspect_only_reads_requested_json_and_extract_keeps_original_bytes() {
        let fixture = Fixture::new();
        let original = b"GIF89a-original-all-frames";
        let archive = fixture.archive(&[
            ("meta.json", br#"{"schemaVersion":1}"#),
            ("images/old-bare-id", original),
            ("ignore.bin", b"unrelated"),
        ]);
        let inspected =
            run(&json!({"command":"inspect", "archive":archive, "jsonPaths":["meta.json"]}))
                .unwrap();
        assert_eq!(inspected["entries"].as_array().unwrap().len(), 3);
        assert_eq!(inspected["jsonFiles"]["meta.json"]["schemaVersion"], 1);
        let target = fixture.0.join("opaque-source");
        run(&json!({"command":"extract", "archive":archive, "entries":[{"path":"images/old-bare-id", "target":target}]})).unwrap();
        assert_eq!(fs::read(&target).unwrap(), original);
        assert!(run(&json!({"command":"extract", "archive":archive, "entries":[{"path":"images/old-bare-id", "target":target}]})).is_err());
    }

    #[test]
    fn rejects_unsafe_paths_case_aliases_and_symlinks_before_extraction() {
        for name in [
            "../escape",
            "/absolute",
            "C:/windows",
            "a\\b",
            "a//b",
            "a/./b",
            "a\0b",
        ] {
            let fixture = Fixture::new();
            let archive = fixture.archive(&[(name, b"payload")]);
            assert!(
                run(&json!({"command":"inspect", "archive":archive, "jsonPaths":[]})).is_err(),
                "{name}"
            );
        }
        let fixture = Fixture::new();
        let archive = fixture.archive(&[("a", b"one"), ("A", b"two")]);
        assert!(run(&json!({"command":"inspect", "archive":archive, "jsonPaths":[]})).is_err());
        let path = fixture.0.join("link.zip");
        let mut writer = ZipWriter::new(fs::File::create(&path).unwrap());
        writer
            .add_symlink("link", "../outside", SimpleFileOptions::default())
            .unwrap();
        writer.finish().unwrap();
        assert!(run(&json!({"command":"inspect", "archive":path, "jsonPaths":[]})).is_err());
    }

    #[test]
    fn export_is_generic_and_roundtrips_raw_files_and_json() {
        let fixture = Fixture::new();
        let source = fixture.0.join("private-original");
        let bytes = b"<svg xmlns='http://www.w3.org/2000/svg' width='1' height='1'/>";
        fs::write(&source, bytes).unwrap();
        let metadata = fixture.0.join("private-json");
        fs::write(&metadata, br#"{"example":true}"#).unwrap();
        let target = fixture.0.join("export.zip");
        let result = run(&json!({"command":"export", "output":target, "files":[{"path":"art/example.svg", "source":source}, {"path":"configuration.json", "source":metadata}]})).unwrap();
        assert_eq!(result["exported"], 2);
        let mut zip = ZipArchive::new(fs::File::open(target).unwrap()).unwrap();
        let mut actual = Vec::new();
        zip.by_name("art/example.svg")
            .unwrap()
            .read_to_end(&mut actual)
            .unwrap();
        assert_eq!(actual, bytes);
    }
}
