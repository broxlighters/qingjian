//! Unix socket 服务：连接内会话编号隔离、握手校验、断线回收和私有权限。
use crate::dispatch::Router;
use qingjian_platform::protocol::{
    ClientMessage, Frame, PROTOCOL_VERSION, ServerMessage, SessionId, read_message, write_message,
};
use std::collections::HashMap;
use std::io;
use std::os::fd::AsRawFd;
use std::os::unix::fs::{FileTypeExt, MetadataExt, PermissionsExt};
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::mpsc::{self, SyncSender};
use std::thread;
use std::time::Duration;

/// 主线程请求队列；容量有限，损坏客户端不能无限占用内存。
type Request = (ClientMessage, mpsc::Sender<Option<ServerMessage>>);
static STOP: AtomicBool = AtomicBool::new(false);
static CONNECTIONS: AtomicUsize = AtomicUsize::new(0);
/// 主线程在下一次空闲节拍退出，落盘由 Router::drop 完成。
pub fn request_shutdown() {
    STOP.store(true, Ordering::Relaxed);
}
static NEXT_SESSION: AtomicU64 = AtomicU64::new(1);

/// 取得 Linux socket 路径；没有运行时目录时使用按用户隔离的私有临时目录。
pub fn socket_path() -> PathBuf {
    std::env::var_os("QINGJIAN_SOCKET")
        .map(PathBuf::from)
        .unwrap_or_else(|| {
            std::env::var_os("XDG_RUNTIME_DIR")
                .filter(|p| !p.is_empty())
                .map(PathBuf::from)
                .unwrap_or_else(|| {
                    PathBuf::from(format!("/tmp/qingjian-{}", unsafe { libc::geteuid() }))
                })
                .join("qingjian.sock")
        })
}

/// 仅移除同用户、确认拒绝连接的陈旧 socket；拒绝符号链接和不安全的父目录。
pub fn bind_socket(path: &Path) -> io::Result<UnixListener> {
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .ok_or_else(|| io::Error::other("socket requires an absolute parent directory"))?;
    if !path.is_absolute() {
        return Err(io::Error::other("socket path must be absolute"));
    }
    if !parent.exists() {
        std::fs::create_dir_all(parent)?;
        std::fs::set_permissions(parent, std::fs::Permissions::from_mode(0o700))?;
    }
    let owner = unsafe { libc::geteuid() };
    let metadata = std::fs::symlink_metadata(parent)?;
    if !metadata.is_dir() || metadata.uid() != owner || metadata.mode() & 0o022 != 0 {
        return Err(io::Error::other(
            "socket directory must be owned by this user and not writable by others",
        ));
    }
    match std::fs::symlink_metadata(path) {
        Ok(meta) => {
            if !meta.file_type().is_socket() || meta.uid() != owner {
                return Err(io::Error::other("unsafe socket path"));
            }
            match UnixStream::connect(path) {
                Ok(_) => {
                    return Err(io::Error::new(
                        io::ErrorKind::AddrInUse,
                        "server already running",
                    ));
                }
                Err(error) if error.kind() == io::ErrorKind::ConnectionRefused => {
                    std::fs::remove_file(path)?
                }
                Err(error) => return Err(error),
            }
        }
        Err(error) if error.kind() == io::ErrorKind::NotFound => {}
        Err(error) => return Err(error),
    }
    let listener = UnixListener::bind(path)?;
    std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600))?;
    Ok(listener)
}

pub fn serve_socket(path: impl AsRef<Path>, router: &mut Router) -> io::Result<()> {
    let listener = bind_socket(path.as_ref())?;
    let (sender, receiver) = mpsc::sync_channel::<Request>(128);
    thread::spawn(move || {
        for stream in listener.incoming() {
            match stream {
                Ok(stream) => {
                    if CONNECTIONS.fetch_add(1, Ordering::Relaxed) >= 64 {
                        CONNECTIONS.fetch_sub(1, Ordering::Relaxed);
                        continue;
                    }
                    let sender = sender.clone();
                    thread::spawn(move || {
                        serve_connection(stream, sender);
                        CONNECTIONS.fetch_sub(1, Ordering::Relaxed);
                    });
                }
                Err(error) => {
                    tracing::error!(%error, "接受客户端连接失败");
                    break;
                }
            }
        }
    });
    while !STOP.load(Ordering::Relaxed) {
        match receiver.recv_timeout(Duration::from_secs(1)) {
            Ok((message, reply)) => {
                let _ = reply.send(router.handle(message));
            }
            Err(mpsc::RecvTimeoutError::Timeout) => router.tick(),
            Err(mpsc::RecvTimeoutError::Disconnected) => return Ok(()),
        }
    }
    std::fs::remove_file(path.as_ref())?;
    Ok(())
}

fn dispatch(sender: &SyncSender<Request>, message: ClientMessage) -> Option<ServerMessage> {
    let (reply, receiver) = mpsc::channel();
    sender.send((message, reply)).ok()?;
    receiver.recv().ok().flatten()
}

fn serve_connection(mut stream: UnixStream, sender: SyncSender<Request>) {
    let mut credentials = libc::ucred {
        pid: 0,
        uid: 0,
        gid: 0,
    };
    let mut size = std::mem::size_of::<libc::ucred>() as libc::socklen_t;
    let trusted = unsafe {
        libc::getsockopt(
            stream.as_raw_fd(),
            libc::SOL_SOCKET,
            libc::SO_PEERCRED,
            (&mut credentials as *mut libc::ucred).cast(),
            &mut size,
        ) == 0
            && credentials.uid == libc::geteuid()
    };
    if !trusted {
        return;
    }
    // 阻塞写上限：不读取答复的客户端不能无限占住线程。
    let _ = stream.set_write_timeout(Some(Duration::from_secs(1)));
    let mut sessions = HashMap::<SessionId, SessionId>::new();
    while let Ok(Some(message)) = read_message::<_, ClientMessage>(&mut stream) {
        let response = match message {
            ClientMessage::OpenSession {
                session,
                app,
                protocol,
            } => {
                if protocol != PROTOCOL_VERSION || sessions.len() >= 64 {
                    break;
                }
                if let Some(old) = sessions.remove(&session) {
                    dispatch(&sender, ClientMessage::CloseSession { session: old });
                }
                let global = SessionId(NEXT_SESSION.fetch_add(1, Ordering::Relaxed));
                sessions.insert(session, global);
                dispatch(
                    &sender,
                    ClientMessage::OpenSession {
                        session: global,
                        app,
                        protocol,
                    },
                );
                Some(ServerMessage::Update {
                    session,
                    frame: Frame::default(),
                })
            }
            ClientMessage::Key { session, event } => {
                let Some(global) = sessions.get(&session) else {
                    break;
                };
                dispatch(
                    &sender,
                    ClientMessage::Key {
                        session: *global,
                        event,
                    },
                )
                .map(|reply| localize(reply, session))
            }
            ClientMessage::Poll { session } => {
                let Some(global) = sessions.get(&session) else {
                    break;
                };
                dispatch(&sender, ClientMessage::Poll { session: *global })
                    .map(|reply| localize(reply, session))
            }
            ClientMessage::Commit { session } => {
                let Some(global) = sessions.get(&session) else {
                    break;
                };
                dispatch(&sender, ClientMessage::Commit { session: *global })
                    .map(|reply| localize(reply, session))
            }
            ClientMessage::Privacy { session, private } => {
                let Some(global) = sessions.get(&session) else {
                    break;
                };
                dispatch(
                    &sender,
                    ClientMessage::Privacy {
                        session: *global,
                        private,
                    },
                );
                None
            }
            ClientMessage::CloseSession { session } => {
                let Some(global) = sessions.remove(&session) else {
                    break;
                };
                dispatch(&sender, ClientMessage::CloseSession { session: global });
                None
            }
            _ => break,
        };
        if let Some(response) = response
            && write_message(&mut stream, &response).is_err()
        {
            break;
        }
    }
    for (_, session) in sessions {
        dispatch(&sender, ClientMessage::CloseSession { session });
    }
}

fn localize(mut response: ServerMessage, local: SessionId) -> ServerMessage {
    match &mut response {
        ServerMessage::KeyResult { session, .. }
        | ServerMessage::Update { session, .. }
        | ServerMessage::Committed { session, .. }
        | ServerMessage::ModeSync { session, .. }
        | ServerMessage::RequestSelection { session, .. } => *session = local,
    }
    response
}
