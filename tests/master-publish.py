"""Real master + real engine advertisement lifecycle, not synthetic registrations."""
import argparse
import importlib.util
from pathlib import Path
import socket
import subprocess
import tempfile
import time

spec = importlib.util.spec_from_file_location("master_wire", Path(__file__).with_name("master-loopback.py"))
wire = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wire)


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def wait(check, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError("Deadline exceeded: " + str(check))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ("master", "engine", "iwad"):
        parser.add_argument("--" + field, type=Path, required=True)
    args = parser.parse_args()
    master, engine, iwad = (p.resolve(strict=True) for p in (args.master, args.engine, args.iwad))
    with tempfile.TemporaryDirectory(prefix="dptdoom-publish-") as temporary:
        root = Path(temporary)
        processes, files = [], []

        def start(name, command):
            work = root / name
            work.mkdir()
            file = (work / "stdout.log").open("wb")
            files.append(file)
            process = subprocess.Popen([str(x) for x in command], cwd=work, stdout=file, stderr=file)
            processes.append(process)
            return process, work / "stdout.log"

        try:
            mp, gp = port(), port()
            m, mlog = start("master", [master, "--port", mp, "--lease-seconds", "3", "--relay"])
            wait(lambda: "listening on" in mlog.read_text())

            def rows():
                with socket.create_connection(("127.0.0.1", mp), timeout=4) as sock:
                    sock.sendall(wire.packet(1))
                    status, body = wire.reply(sock, 1)
                    assert status == 0
                    return wire.listing(body)

            common = [engine, "-iwad", iwad, "-warp", "1", "-skill", "4", "-dmflags", "3",
                      "-nomonsters", "-net-timeout", "12000", "-headless-tics", "140"]
            host, hlog = start("host", common + ["-net-host", gp, "-master", f"127.0.0.1:{mp}", "-session-name", "Published test", "-net-relay"])
            advertised = wait(rows)
            assert len(advertised) == 1
            row = advertised[0]
            assert row[24:88].rstrip(b"\0") == b"Published test"
            assert wire.struct.unpack("!H", row[148:150])[0] == gp
            assert row[150:153] == bytes([4, 7, 2])
            # TTL is 3s: keep it listed beyond TTL by real engine heartbeat.
            time.sleep(3.4)
            assert len(rows()) == 1 and host.poll() is None
            assert "heartbeat:" in (root / "master/dptdoom-master.log").read_text()
            session=rows()[0][:16].hex()
            guest, glog = start("guest", common + ["-net-join", f"127.0.0.1:{gp}",'-relay-master',f'127.0.0.1:{mp}','-relay-session',session])
            wait(lambda: not rows())
            assert host.wait(20) == 0, hlog.read_text()
            assert guest.wait(20) == 0, glog.read_text()
            assert "connected as player 1" in hlog.read_text()
            assert "advertisement removed" in hlog.read_text()
            assert 'using relayed game transport' not in hlog.read_text()
            assert 'using relayed game transport' not in glog.read_text()
            assert "connected as player 2" in glog.read_text()
            # Block the direct path logically by pointing guest at an unused
            # loopback port; only the registered relay channel can connect it.
            gp=port();bad_direct=port()
            host,hlog=start('relayed-host',common+['-headless-trace','trace.txt','-net-host',gp,'-master',f'127.0.0.1:{mp}','-net-relay'])
            advertised=wait(lambda: [r for r in rows() if wire.struct.unpack('!I',r[156:160])[0]&1])
            session_id=advertised[0][:16].hex()
            guest,glog=start('relayed-guest',common+['-headless-trace','trace.txt','-net-join',f'127.0.0.1:{bad_direct}',
                '-relay-master',f'127.0.0.1:{mp}','-relay-session',session_id])
            assert host.wait(20)==0,hlog.read_text()
            assert guest.wait(20)==0,glog.read_text()
            assert 'host paired through master' in hlog.read_text()
            assert 'guest paired through master' in glog.read_text()
            assert 'connected as player 1' in hlog.read_text() and 'connected as player 2' in glog.read_text()
            assert not rows()
            assert (root/'relayed-host/trace.txt').read_bytes()==(root/'relayed-guest/trace.txt').read_bytes()
            # Peer exits cleanly at 70 tics; survivor must reach 140 through EOF
            # propagated by the relay, not reset/eject to the menu.
            gp=port();bad_direct=port()
            shorter=[str(x) for x in common];shorter[shorter.index('-headless-tics')+1]='70'
            host,hlog=start('relay-survivor-host',common+['-headless-session-report','-net-host',gp,'-master',f'127.0.0.1:{mp}','-net-relay'])
            advertised=wait(lambda:[r for r in rows() if wire.struct.unpack('!I',r[156:160])[0]&1])
            guest,glog=start('relay-survivor-guest',shorter+['-net-join',f'127.0.0.1:{bad_direct}',
                '-relay-master',f'127.0.0.1:{mp}','-relay-session',advertised[0][:16].hex()])
            assert guest.wait(20)==0,glog.read_text()
            assert host.wait(20)==0,hlog.read_text()
            assert 'Session report: state=0 player=1 leveltime=140' in hlog.read_text(),hlog.read_text()
            assert "connected as player 1" in hlog.read_text()
            assert "connected as player 2" in glog.read_text()
            # Ordinary failed startup invokes shutdown cleanup and removes lease.
            host, hlog = start("timeout", [engine, "-iwad", iwad, "-warp", "1", "-net-host", gp,
                "-net-timeout", "2000", "-headless-dump", "waiting.ppm", "-master", f"127.0.0.1:{mp}"])
            wait(rows)
            assert host.wait(10) != 0
            wait(lambda: not rows())
            ppm=(root/'timeout/waiting.ppm').read_bytes()
            header=b'P6\n640 400\n255\n'
            assert ppm.startswith(header) and len(ppm)==len(header)+640*400*3
            pixels=ppm[len(header):]
            # The startup screen must contain native HUD-font text, not the
            # original all-black platform framebuffer. No screen/desktop capture.
            assert any(pixels[(88*2)*640*3:(110*2)*640*3]),'missing waiting text'
            # Master outage must not prevent a real direct connection.
            dead = port()
            gp = port()
            host, hlog = start("offline", common + ["-net-host", gp, "-master", f"127.0.0.1:{dead}"])
            wait(lambda: "still waiting" in hlog.read_text())
            offline=hlog.read_text()
            for field in ('Master diagnostic v2:', 'request-start', 'request-failed', 'endpoint=127.0.0.1:', 'stage=connect', 'os_error=', 'retry_ms=10000'):
                assert field in offline, (field, offline)
            guest, glog = start("offline-guest", common + ["-net-join", f"127.0.0.1:{gp}"])
            assert host.wait(20) == 0, hlog.read_text()
            assert guest.wait(20) == 0, glog.read_text()
            timeout_text=(root/'timeout/stdout.log').read_text()
            assert 'no second player connected' in timeout_text and 'separate' in timeout_text
            print("Master publication: actual metadata/heartbeat/removal/outage; real engine direct-failure to relay fallback with gameplay OK")
        finally:
            for p in processes:
                if p.poll() is None:
                    p.terminate()
                p.wait(10)
            for f in files:
                f.close()
            for path in root.rglob("*.log"):
                assert "AddressSanitizer" not in path.read_text(errors="replace"), path


if __name__ == "__main__":
    main()
