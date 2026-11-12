//! ZIP transport for a private, already-sanitized diagnostic staging directory.
use std::{
    ffi::{CStr, c_char, c_void},
    fs,
    io::{Read, Write},
    panic::{AssertUnwindSafe, catch_unwind},
    path::Path,
};

fn write_zip(root: &Path, output: &Path, progress: impl Fn(u64) -> bool) -> Result<(), String> {
    fn collect(root: &Path, path: &Path, files: &mut Vec<(String, u64)>) -> Result<(), String> {
        for entry in fs::read_dir(path).map_err(|e| e.to_string())? {
            let entry = entry.map_err(|e| e.to_string())?;
            let info = fs::symlink_metadata(entry.path()).map_err(|e| e.to_string())?;
            if info.is_symlink() {
                return Err("Diagnostic staging contains a link".into());
            }
            if info.is_dir() {
                // The host staging format is flat apart from these three directories.
                if path != root
                    || !["logs", "crash"].contains(&entry.file_name().to_string_lossy().as_ref())
                {
                    return Err("Unexpected diagnostic directory".into());
                }
                collect(root, &entry.path(), files)?;
            } else if info.is_file() {
                let name = entry
                    .path()
                    .strip_prefix(root)
                    .map_err(|e| e.to_string())?
                    .to_str()
                    .ok_or("Non-UTF-8 diagnostic filename")?
                    .replace('\\', "/");
                if name.split('/').any(|part| {
                    part.is_empty() || part == "." || part == ".." || part.contains(':')
                }) {
                    return Err("Invalid diagnostic filename".into());
                }
                files.push((name, info.len()));
                if files.len() > 128
                    || files.iter().map(|(_, size)| size).sum::<u64>() > 512 * 1024 * 1024
                {
                    return Err("Diagnostic package exceeds 512 MiB or 128 files".into());
                }
            } else {
                return Err("Diagnostic staging contains a special file".into());
            }
        }
        Ok(())
    }
    let mut files = Vec::new();
    collect(root, root, &mut files)?;
    files.sort_by(|a, b| a.0.cmp(&b.0));
    let file = fs::OpenOptions::new()
        .write(true)
        .read(true)
        .create_new(true)
        .open(output)
        .map_err(|e| e.to_string())?;
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        file.set_permissions(fs::Permissions::from_mode(0o600))
            .map_err(|e| e.to_string())?;
    }
    let mut zip = zip::ZipWriter::new(file);
    let mut copied = 0;
    for (name, size) in files {
        if !progress(copied) {
            return Err("Diagnostic export cancelled".into());
        }
        zip.start_file(
            &name,
            zip::write::SimpleFileOptions::default()
                .compression_method(zip::CompressionMethod::Deflated)
                .unix_permissions(0o600),
        )
        .map_err(|e| e.to_string())?;
        let mut input = fs::File::open(root.join(&name)).map_err(|e| e.to_string())?;
        let mut remaining = size;
        let mut bytes = [0u8; 64 * 1024];
        while remaining > 0 {
            if !progress(copied) {
                return Err("Diagnostic export cancelled".into());
            }
            let limit = remaining.min(bytes.len() as u64) as usize;
            let length = input.read(&mut bytes[..limit]).map_err(|e| e.to_string())?;
            if length == 0 {
                return Err("Diagnostic staging changed".into());
            }
            zip.write_all(&bytes[..length]).map_err(|e| e.to_string())?;
            remaining -= length as u64;
            copied += length as u64;
        }
    }
    let mut output = zip.finish().map_err(|e| e.to_string())?;
    output.flush().map_err(|e| e.to_string())?;
    if !progress(copied) {
        return Err("Diagnostic export cancelled".into());
    }
    Ok(())
}

/// Synchronous ZIP writer. The Qt host owns staging, cleanup and atomic publication.
/// No upload, runtime, or global state. Returns only after all callbacks have ended.
/// # Safety
/// Paths are borrowed NUL-terminated UTF-8 strings. The error buffer has
/// error_capacity writable bytes. Callback/context remain valid until return;
/// the callback may not unwind and must not call back into this function.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_diagnostic_zip(
    staging: *const c_char,
    output: *const c_char,
    progress: Option<unsafe extern "C" fn(*mut c_void, u64) -> bool>,
    context: *mut c_void,
    error: *mut u8,
    error_capacity: usize,
) -> bool {
    let result = catch_unwind(AssertUnwindSafe(|| {
        if staging.is_null() || output.is_null() {
            return Err("Missing diagnostic path".to_owned());
        }
        let staging = unsafe { CStr::from_ptr(staging) }
            .to_str()
            .map_err(|e| e.to_string())?;
        let output = unsafe { CStr::from_ptr(output) }
            .to_str()
            .map_err(|e| e.to_string())?;
        write_zip(Path::new(staging), Path::new(output), |bytes| {
            progress.is_none_or(|callback| unsafe { callback(context, bytes) })
        })
    }))
    .unwrap_or_else(|_| Err("Diagnostic ZIP panic".into()));
    if let Err(message) = result {
        if !error.is_null() && error_capacity > 0 {
            let length = message.len().min(error_capacity - 1);
            unsafe {
                std::ptr::copy_nonoverlapping(message.as_ptr(), error, length);
                *error.add(length) = 0;
            }
        }
        false
    } else {
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn zip_is_readable_and_cancellable() {
        let root = std::env::temp_dir().join(format!("echo-diagnostics-{}", std::process::id()));
        fs::create_dir_all(root.join("logs")).unwrap();
        fs::write(root.join("logs/run.log"), "中文日志").unwrap();
        let output = root.with_extension("zip");
        let _ = fs::remove_file(&output);
        write_zip(&root, &output, |_| true).unwrap();
        let mut archive = zip::ZipArchive::new(fs::File::open(&output).unwrap()).unwrap();
        let mut log = String::new();
        archive
            .by_name("logs/run.log")
            .unwrap()
            .read_to_string(&mut log)
            .unwrap();
        assert_eq!(log, "中文日志");
        drop(archive);
        fs::remove_file(&output).unwrap();
        assert!(write_zip(&root, &output, |_| false).is_err());
        fs::remove_file(output).unwrap();
        fs::remove_dir_all(root).unwrap();
    }
}
