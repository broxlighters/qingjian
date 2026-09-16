//! 真 Unix socket 回环：连接编号隔离、握手失败、重启与文件权限。
#![cfg(target_os = "linux")]
use qingjian_linux_server::ipc::bind_socket;
use qingjian_platform::protocol::{
    ClientMessage, KeyEvent, KeyModifiers, PROTOCOL_VERSION, ServerMessage, SessionId,
    read_message, write_message,
};
use std::os::unix::fs::{MetadataExt, PermissionsExt};
use std::os::unix::net::UnixStream;
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant};

struct Server {
    child: Child,
    directory: PathBuf,
}
impl Server {
    fn start(directory: PathBuf) -> Self {
        std::fs::create_dir_all(&directory).unwrap();
        std::fs::set_permissions(&directory, std::fs::Permissions::from_mode(0o700)).unwrap();
        let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../..");
        let child = Command::new(env!("CARGO_BIN_EXE_qingjian-linux-server"))
            .env("QINGJIAN_SOCKET", directory.join("server.sock"))
            .env("QINGJIAN_RESOURCES", &root)
            .env("QINGJIAN_DICT", root.join("assets/sample/dict.tsv"))
            .env("XDG_CONFIG_HOME", directory.join("config"))
            .env("XDG_DATA_HOME", directory.join("data"))
            .env("XDG_STATE_HOME", directory.join("state"))
            .stdout(Stdio::null())
            .spawn()
            .unwrap();
        Self { child, directory }
    }
    fn connect(&mut self) -> UnixStream {
        let started = Instant::now();
        loop {
            assert!(
                self.child.try_wait().unwrap().is_none(),
                "server exited early"
            );
            if let Ok(stream) = UnixStream::connect(self.directory.join("server.sock")) {
                stream
                    .set_read_timeout(Some(Duration::from_secs(2)))
                    .unwrap();
                return stream;
            }
            assert!(started.elapsed() < Duration::from_secs(5));
            std::thread::sleep(Duration::from_millis(10));
        }
    }
}
impl Drop for Server {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}
fn open(stream: &mut UnixStream) {
    write_message(
        stream,
        &ClientMessage::OpenSession {
            session: SessionId(1),
            app: None,
            protocol: PROTOCOL_VERSION,
        },
    )
    .unwrap();
    assert!(matches!(
        read_message::<_, ServerMessage>(stream).unwrap(),
        Some(ServerMessage::Update {
            session: SessionId(1),
            ..
        })
    ));
}
fn text(stream: &mut UnixStream, value: &str) {
    for c in value.chars() {
        write_message(
            stream,
            &ClientMessage::Key {
                session: SessionId(1),
                event: KeyEvent::new(c as u32, Some(c), KeyModifiers::default()),
            },
        )
        .unwrap();
        assert!(matches!(
            read_message::<_, ServerMessage>(stream).unwrap(),
            Some(ServerMessage::KeyResult {
                session: SessionId(1),
                ..
            })
        ));
    }
}
fn commit(stream: &mut UnixStream) -> Option<String> {
    write_message(
        stream,
        &ClientMessage::Commit {
            session: SessionId(1),
        },
    )
    .unwrap();
    match read_message::<_, ServerMessage>(stream).unwrap() {
        Some(ServerMessage::Committed { text, .. }) => text,
        _ => panic!("commit result"),
    }
}
#[test]
fn connections_are_isolated_and_restart_reopens_cleanly() {
    let directory = std::env::temp_dir().join(format!("qingjian-socket-{}", std::process::id()));
    let mut server = Server::start(directory.clone());
    let mut first = server.connect();
    let mut second = server.connect();
    open(&mut first);
    open(&mut second);
    text(&mut first, "ni");
    text(&mut second, "kai");
    assert_eq!(commit(&mut first).as_deref(), Some("ni"));
    drop(first);
    assert_eq!(commit(&mut second).as_deref(), Some("kai"));
    let mut bad = server.connect();
    write_message(
        &mut bad,
        &ClientMessage::OpenSession {
            session: SessionId(1),
            app: None,
            protocol: 0,
        },
    )
    .unwrap();
    assert_eq!(read_message::<_, ServerMessage>(&mut bad).unwrap(), None);
    assert_eq!(
        std::fs::metadata(directory.join("server.sock"))
            .unwrap()
            .mode()
            & 0o777,
        0o600
    );
    drop(server);
    let mut restarted = Server::start(directory.clone());
    let mut third = restarted.connect();
    open(&mut third);
    assert_eq!(commit(&mut third), None);
    text(&mut third, "nihao");
    assert_eq!(commit(&mut third).as_deref(), Some("nihao"));
    drop(restarted);
    std::fs::remove_dir_all(directory).unwrap();
}
#[test]
fn socket_path_does_not_replace_regular_files_or_symlinks() {
    let directory = std::env::temp_dir().join(format!("qingjian-bind-{}", std::process::id()));
    std::fs::create_dir_all(&directory).unwrap();
    let path = directory.join("server.sock");
    std::fs::write(&path, "keep").unwrap();
    assert!(bind_socket(&path).is_err());
    assert_eq!(std::fs::read_to_string(&path).unwrap(), "keep");
    std::fs::remove_file(&path).unwrap();
    std::os::unix::fs::symlink(directory.join("missing"), &path).unwrap();
    assert!(bind_socket(&path).is_err());
    std::fs::remove_dir_all(directory).unwrap();
}

#[test]
fn linux_ui_negotiates_after_legacy_open_and_binds_ack_to_connection() {
    use serde_json::{Value, json};
    let directory = std::env::temp_dir().join(format!("qingjian-ui-socket-{}", std::process::id()));
    let mut server = Server::start(directory.clone());
    let mut stream = server.connect();
    write_message(
        &mut stream,
        &ClientMessage::OpenSession {
            session: SessionId(1),
            app: None,
            protocol: PROTOCOL_VERSION,
        },
    )
    .unwrap();
    let opened = read_message::<_, Value>(&mut stream).unwrap().unwrap();
    assert_eq!(opened["Update"]["linux_ui"]["version"], 1);
    assert_eq!(opened["Update"]["linux_ui"]["renderer"], "fcitx");
    write_message(
        &mut stream,
        &json!({"LinuxHello": {"version": 1, "generation": 9, "context": "test-context"}}),
    )
    .unwrap();
    assert_eq!(
        read_message::<_, Value>(&mut stream).unwrap().unwrap()["LinuxHello"]["version"],
        1
    );
    write_message(
        &mut stream,
        &ClientMessage::Privacy {
            session: SessionId(1),
            private: false,
        },
    )
    .unwrap();
    write_message(
        &mut stream,
        &ClientMessage::Key {
            session: SessionId(1),
            event: KeyEvent::new(78, Some('n'), KeyModifiers::default()),
        },
    )
    .unwrap();
    let response = read_message::<_, Value>(&mut stream).unwrap().unwrap();
    let mut identity = response["KeyResult"]["identity"].clone();
    assert_eq!(identity["generation"], 9);
    assert_eq!(identity["context"], "test-context");
    assert_eq!(response["KeyResult"]["session"], 1);
    write_message(
        &mut stream,
        &json!({"DisplayAcknowledged": {"session": 1, "identity": identity, "senses": []}}),
    )
    .unwrap();
    assert_eq!(commit(&mut stream).as_deref(), Some("n"));
    identity["generation"] = json!(8);
    write_message(
        &mut stream,
        &json!({"DisplayAcknowledged": {"session": 1, "identity": identity, "senses": []}}),
    )
    .unwrap();
    assert_eq!(read_message::<_, Value>(&mut stream).unwrap(), None);
    drop(server);
    std::fs::remove_dir_all(directory).unwrap();
}
