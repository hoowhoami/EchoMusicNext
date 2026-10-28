//! Bounded ZIP extraction for trusted plugin packages. This provides integrity,
//! not isolation. The destination must be a fresh host-owned staging directory.
use std::{
    collections::HashSet,
    ffi::CStr,
    fs,
    io::{Read, Write},
    panic::{AssertUnwindSafe, catch_unwind},
    path::{Component, Path},
};

fn extract(archive: &Path, destination: &Path) -> Result<(), String> {
    if fs::metadata(archive).map_err(|e| e.to_string())?.len() > 128 * 1024 * 1024 {
        return Err("ZIP exceeds 128 MiB".into());
    }
    if !destination.is_dir()
        || fs::read_dir(destination)
            .map_err(|e| e.to_string())?
            .next()
            .is_some()
    {
        return Err("Extraction requires an empty staging directory".into());
    }
    let file = fs::File::open(archive).map_err(|e| e.to_string())?;
    let mut zip = zip::ZipArchive::new(file).map_err(|e| e.to_string())?;
    if zip.len() > 8192 {
        return Err("ZIP exceeds 8192 entries".into());
    }
    let mut names = HashSet::new();
    let mut bytes = 0u64;
    for i in 0..zip.len() {
        let mut entry = zip.by_index(i).map_err(|e| e.to_string())?;
        let name = entry.name().to_owned();
        let path = Path::new(&name);
        // Explicitly reject Windows separators/device paths on every platform.
        if name.contains('\\')
            || name.contains(':')
            || name.contains('\0')
            || path
                .components()
                .any(|c| !matches!(c, Component::Normal(_)))
        {
            return Err("Unsafe ZIP path".into());
        }
        let normalized = name.trim_end_matches('/').to_lowercase();
        if !names.insert(normalized) {
            return Err("Duplicate ZIP path".into());
        }
        let permissions = entry.unix_mode().unwrap_or(0o644);
        let mode = permissions & 0o170000;
        if mode != 0 && mode != 0o100000 && mode != 0o040000 {
            return Err("ZIP links/special files are not supported".into());
        }
        let target = destination.join(path);
        if entry.is_dir() {
            fs::create_dir_all(target).map_err(|e| e.to_string())?;
            continue;
        }
        bytes = bytes.checked_add(entry.size()).ok_or("ZIP size overflow")?;
        if bytes > 128 * 1024 * 1024 {
            return Err("Expanded ZIP exceeds 128 MiB".into());
        }
        fs::create_dir_all(target.parent().ok_or("Invalid ZIP path")?)
            .map_err(|e| e.to_string())?;
        let mut output = fs::OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&target)
            .map_err(|e| e.to_string())?;
        let expected = entry.size();
        let mut limited = (&mut entry).take(expected + 1);
        let copied = std::io::copy(&mut limited, &mut output).map_err(|e| e.to_string())?;
        if copied != expected {
            return Err("ZIP size mismatch".into());
        }
        output.flush().map_err(|e| e.to_string())?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            // Retain only execute bits; never grant setuid/setgid or writable packages.
            output
                .set_permissions(fs::Permissions::from_mode(0o644 | (permissions & 0o111)))
                .map_err(|e| e.to_string())?;
        }
    }
    Ok(())
}

/// Synchronous worker-only bounded ZIP extraction. UTF-8 paths and caller-owned
/// error buffer are borrowed for this call; no callbacks or retained pointers.
/// # Safety
/// Paths must be NUL-terminated; error must point to error_capacity writable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn echo_plugin_extract_zip(
    archive: *const std::ffi::c_char,
    destination: *const std::ffi::c_char,
    error: *mut u8,
    error_capacity: usize,
) -> bool {
    let result = catch_unwind(AssertUnwindSafe(|| {
        if archive.is_null() || destination.is_null() {
            return Err("Missing ZIP path".to_string());
        }
        let archive = unsafe { CStr::from_ptr(archive) }
            .to_str()
            .map_err(|e| e.to_string())?;
        let destination = unsafe { CStr::from_ptr(destination) }
            .to_str()
            .map_err(|e| e.to_string())?;
        extract(Path::new(archive), Path::new(destination))
    }))
    .unwrap_or_else(|_| Err("ZIP extraction panic".into()));
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
    use zip::{ZipWriter, write::SimpleFileOptions};
    #[cfg(unix)]
    #[test]
    fn zip_preserves_execution_without_privilege_bits() {
        use std::os::unix::fs::PermissionsExt;
        let root = std::env::temp_dir().join(format!("echo-zip-execute-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(root.join("staging")).unwrap();
        let mut zip = ZipWriter::new(fs::File::create(root.join("helper.echomod")).unwrap());
        zip.start_file(
            "helper",
            SimpleFileOptions::default().unix_permissions(0o4755),
        )
        .unwrap();
        zip.write_all(b"helper bytes").unwrap();
        zip.finish().unwrap();
        extract(&root.join("helper.echomod"), &root.join("staging")).unwrap();
        assert_eq!(
            fs::metadata(root.join("staging/helper"))
                .unwrap()
                .permissions()
                .mode()
                & 0o7777,
            0o755
        );
        fs::remove_dir_all(root).unwrap();
    }
    #[test]
    fn zip_rejects_duplicates_windows_paths_and_links() {
        for (case, names, mode) in [
            ("duplicate", vec!["a", "A"], 0o644),
            ("windows", vec!["C:\\escape"], 0o644),
            ("link", vec!["link"], 0o120777),
        ] {
            let root = std::env::temp_dir().join(format!("echo-zip-{}-{case}", std::process::id()));
            let _ = fs::remove_dir_all(&root);
            fs::create_dir_all(root.join("staging")).unwrap();
            let mut zip = ZipWriter::new(fs::File::create(root.join("bad.zip")).unwrap());
            for name in names {
                if case == "link" {
                    zip.add_symlink(name, "outside", SimpleFileOptions::default())
                        .unwrap();
                } else {
                    zip.start_file(name, SimpleFileOptions::default().unix_permissions(mode))
                        .unwrap();
                    zip.write_all(b"payload").unwrap();
                }
            }
            zip.finish().unwrap();
            assert!(extract(&root.join("bad.zip"), &root.join("staging")).is_err());
            fs::remove_dir_all(root).unwrap();
        }
    }
    #[test]
    fn zip_rejects_traversal_and_preserves_external_files() {
        let root = std::env::temp_dir().join(format!("echo-zip-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(root.join("staging")).unwrap();
        fs::write(root.join("outside"), b"original").unwrap();
        let mut zip = ZipWriter::new(fs::File::create(root.join("bad.zip")).unwrap());
        zip.start_file("../outside", SimpleFileOptions::default())
            .unwrap();
        zip.write_all(b"changed").unwrap();
        zip.finish().unwrap();
        assert!(extract(&root.join("bad.zip"), &root.join("staging")).is_err());
        assert_eq!(fs::read(root.join("outside")).unwrap(), b"original");
        fs::remove_dir_all(root).unwrap();
    }
}
