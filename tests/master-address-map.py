"""Native master config + registration/list/auth over real TCP; simulated hairpin."""
import importlib.util
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

spec=importlib.util.spec_from_file_location('wire',Path(__file__).with_name('master-loopback.py'))
wire=importlib.util.module_from_spec(spec);spec.loader.exec_module(wire)
exe=str(Path(sys.argv[1]).resolve(strict=True))
with tempfile.TemporaryDirectory(prefix='dptdoom-hairpin-') as temporary:
    root=Path(temporary)
    config=root/'master.conf'
    for invalid in ('address-map=0.0.0.0/0=203.0.113.9', 'unknown=value',
                    'address-map=192.168.1.0/24=203.0.113.9\naddress-map=192.168.1.0/24=203.0.113.10'):
        config.write_text(invalid)
        result=subprocess.run([exe,'--config',str(config)],cwd=root,capture_output=True,timeout=10)
        assert result.returncode!=0 and b'invalid config line' in result.stderr,result
    for invalid in ('public-address-mode=dns\n',
                    'public-address-mode=api\npublic-api=http://example.org\nhairpin-subnet=127.0.0.1/32\n',
                    'public-address-mode=manual\npublic-ip=10.0.0.1\nhairpin-subnet=127.0.0.1/32\n'):
        config.write_text(invalid)
        result=subprocess.run([exe,'--config',str(config)],cwd=root,capture_output=True,timeout=10)
        assert result.returncode!=0 and b'invalid public address' in result.stderr,result
    # A bad bind address from the config must fail loudly instead of silently
    # falling back to loopback, which would look like "the server is up" while
    # no remote client can ever connect.
    config.write_text('bind=not-an-ip\n')
    result=subprocess.run([exe,'--config',str(config)],cwd=root,capture_output=True,timeout=10)
    assert result.returncode!=0 and b'must be numeric IPv4' in result.stdout+result.stderr,result
    # A config path that does not exist yet is created as a commented template,
    # so a first run gives something to edit instead of an error.
    fresh=root/'fresh.conf'
    assert not fresh.exists()
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
    with (root/'fresh.log').open('wb') as log:
        process=subprocess.Popen([exe,'--port',str(port),'--config',str(fresh)],
                                 cwd=root,stdout=log,stderr=log)
        try:
            deadline=time.monotonic()+10
            while b'listening on' not in (root/'fresh.log').read_bytes():
                assert process.poll() is None
                assert time.monotonic()<deadline
                time.sleep(.02)
        finally:
            process.terminate();process.wait(10)
    text=(root/'fresh.log').read_bytes()
    assert b'wrote a commented template' in text,text
    body=fresh.read_text()
    # The template must be inert: every directive commented out, otherwise the
    # master would silently start with settings the operator never chose.
    assert body.strip(), 'template is empty'
    directives=[l for l in body.splitlines() if l.strip() and not l.lstrip().startswith('#')]
    assert not directives, directives
    assert 'bind=0.0.0.0' in body and 'address-map=' in body, body
    # A mistyped directory must still fail rather than "succeed" with a master
    # that ignores an existing config elsewhere.
    missing=root/'nosuchdir'/'x.conf'
    result=subprocess.run([exe,'--config',str(missing)],cwd=root,capture_output=True,timeout=10)
    assert result.returncode!=0,result

    # Ban list: loaded from disk, applied to the TCP source address, and a
    # malformed rule fails startup instead of loading a partial list.
    def with_bans(name, body):
        bans=root/(name+'.txt')
        if body is not None:
            bans.write_text(body)
        conf=root/(name+'.conf')
        conf.write_text('banlist=%s\n'%bans)
        return conf

    def try_connect(conf, name):
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        logpath=root/(name+'.log')
        with logpath.open('wb') as log:
            process=subprocess.Popen([exe,'--port',str(port),'--config',str(conf),
                                      '--log',str(root/(name+'-events.log'))],
                                     cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in logpath.read_bytes():
                    assert process.poll() is None, logpath.read_bytes()
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                rejected=False
                try:
                    with socket.create_connection(('127.0.0.1',port),timeout=3) as s:
                        s.sendall(wire.packet(1,b''))
                        wire.reply(s,1)
                except (OSError,AssertionError):
                    rejected=True
                return rejected, logpath.read_bytes()
            finally:
                process.terminate();process.wait(10)

    # No ban file: nothing banned.
    rejected,text=try_connect(with_bans('ban-none',None),'ban-none')
    assert not rejected,text
    # Comment-only file: nothing banned.
    rejected,text=try_connect(with_bans('ban-empty','# nothing\n\n'),'ban-empty')
    assert not rejected,text
    # A rule that does not match the client must not block it.
    rejected,text=try_connect(with_bans('ban-other','198.51.100.1\n'),'ban-other')
    assert not rejected,text
    # Banning loopback blocks a loopback client, and the reason is logged.
    rejected,text=try_connect(with_bans('ban-self','# block local\n127.0.0.1\n'),'ban-self')
    assert rejected,text
    assert b'banned-source' in text,text
    assert b'scope=control' in text,text

    # Scope: the same source can be banned on one port and still served on the
    # other. Without --relay-port every connection is control-scoped, so a
    # relay-only rule must block nothing at all - that is the property that
    # makes the feature safe to add to an existing file.
    rejected,text=try_connect(with_bans('ban-scope-relay','127.0.0.1 scope relay\n'),'ban-scope-relay')
    assert not rejected,text
    rejected,text=try_connect(with_bans('ban-scope-control','127.0.0.1 scope control\n'),'ban-scope-control')
    assert rejected and b'scope=control' in text,text

    # With a dedicated relay port the two scopes separate for real: a relay-only
    # ban kills the relay port while the control port keeps answering, and a
    # control-only ban does the opposite.
    def scoped_ports(rule, label):
        with socket.socket() as r:r.bind(('127.0.0.1',0));control_port=r.getsockname()[1]
        with socket.socket() as r:r.bind(('127.0.0.1',0));relay_port=r.getsockname()[1]
        conf=root/(label+'.conf')
        bans=root/(label+'.txt')
        bans.write_text(rule)
        conf.write_text('banlist=%s\n'%bans)
        logpath=root/(label+'.log')
        with logpath.open('wb') as log:
            process=subprocess.Popen([exe,'--port',str(control_port),'--relay',
                                      '--relay-port',str(relay_port),'--config',str(conf),
                                      '--log',str(root/(label+'-events.log'))],
                                     cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'relay listening on' not in logpath.read_bytes():
                    assert process.poll() is None,logpath.read_bytes()
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                def reachable(port):
                    try:
                        with socket.create_connection(('127.0.0.1',port),timeout=3) as s:
                            s.sendall(wire.packet(1,b''))
                            wire.reply(s,1)
                        return True
                    except (OSError,AssertionError):
                        return False
                return reachable(control_port),reachable(relay_port)
            finally:
                process.terminate();process.wait(10)

    control_ok,relay_ok=scoped_ports('127.0.0.1 scope relay\n','scope-relay-port')
    assert control_ok and not relay_ok,(control_ok,relay_ok)
    control_ok,relay_ok=scoped_ports('127.0.0.1 scope control\n','scope-control-port')
    assert not control_ok and relay_ok,(control_ok,relay_ok)
    control_ok,relay_ok=scoped_ports('127.0.0.1\n','scope-both-port')
    assert not control_ok and not relay_ok,(control_ok,relay_ok)

    # Malformed rule: whole reload fails, no partial list is applied.
    bad=with_bans('ban-bad','127.0.0.1\nnot-an-ip\n')
    result=subprocess.run([exe,'--port','25177','--config',str(bad)],
                          cwd=root,capture_output=True,text=True,timeout=10)
    assert result.returncode!=0,result
    assert 'invalid ban rule' in result.stderr,result

    # Runtime reload: editing the file takes effect without a restart, and a
    # malformed edit keeps the previous list instead of weakening it.
    def check_live_reload():
        bans=root/'ban-live.txt'
        bans.write_text('# empty\n')
        conf=root/'ban-live.conf'
        conf.write_text('banlist=%s\n'%bans)
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        logpath=root/'ban-live.log'

        def live_ok():
            try:
                with socket.create_connection(('127.0.0.1',port),timeout=3) as s:
                    s.sendall(wire.packet(1,b''))
                    wire.reply(s,1)
                    return True
            except OSError:
                return False

        def wait_until(predicate, timeout=5.0):
            # Poll instead of sleeping a fixed interval: the master checks the
            # file once a second, so this returns as soon as the change lands
            # rather than always paying the worst case.
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                if predicate():
                    return True
                time.sleep(.1)
            return predicate()

        with logpath.open('wb') as log:
            process=subprocess.Popen([exe,'--port',str(port),'--config',str(conf),
                                      '--log',str(root/'ban-live-events.log')],
                                     cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in logpath.read_bytes():
                    assert process.poll() is None, logpath.read_bytes()
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                assert live_ok(), 'ban-live: should connect before any ban'

                bans.write_text('127.0.0.1\n')
                assert wait_until(lambda: not live_ok()), \
                    'ban-live: ban added on disk was not applied'

                bans.write_text('# cleared\n')
                assert wait_until(live_ok), \
                    'ban-live: removed ban was not lifted'

                # Re-ban, then break the file: the previous list must stay.
                bans.write_text('127.0.0.1\n')
                assert wait_until(lambda: not live_ok()), \
                    'ban-live: re-ban not applied'
                bans.write_text('127.0.0.1\nnot-an-ip\n')
                assert wait_until(lambda: b'keeping the previous list' in logpath.read_bytes()), \
                    'ban-live: malformed reload was not reported'
                assert not live_ok(), 'ban-live: bad edit must keep the previous list'
            finally:
                process.terminate();process.wait(10)

    check_live_reload()

    # Reload must not be able to do what startup refuses: an enforced allow list
    # edited down to "no rule for the served port" would silently shut the
    # master, so the previous list is kept instead.
    def check_allow_reload_lockout():
        allow=root/'allow-lock.txt'
        allow.write_text('127.0.0.1\n')
        conf=root/'allow-lock.conf'
        conf.write_text('allowlist=%s\naccess-mode=allowlist\n'%allow)
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        logpath=root/'allow-lock.log'

        def reachable():
            try:
                with socket.create_connection(('127.0.0.1',port),timeout=3) as s:
                    s.sendall(wire.packet(1,b''))
                    wire.reply(s,1)
                    return True
            except (OSError,AssertionError):
                return False

        def wait_until(predicate, timeout=5.0):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                if predicate():
                    return True
                time.sleep(.1)
            return predicate()

        with logpath.open('wb') as log:
            process=subprocess.Popen([exe,'--port',str(port),'--config',str(conf),
                                      '--log',str(root/'allow-lock-events.log')],
                                     cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in logpath.read_bytes():
                    assert process.poll() is None, logpath.read_bytes()
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                assert reachable(), 'allow-lock: listed source should connect'

                # Relay-only rules leave the control port with no permit.
                allow.write_text('127.0.0.1 scope relay\n')
                assert wait_until(lambda: b'would lock a served port out' in logpath.read_bytes()), \
                    'allow-lock: relay-only reload was not refused'
                assert reachable(), 'allow-lock: refused reload must keep the previous list'
                assert process.poll() is None, 'allow-lock: master must stay up'

                # An already-expired permit is not a usable rule either.
                allow.write_text('127.0.0.1 until 2020-01-01T00:00:00Z\n')
                assert wait_until(lambda: b'would lock a served port out' in logpath.read_bytes()), \
                    'allow-lock: expired-only reload was not refused'
                assert reachable(), 'allow-lock: expired rules must not take effect'
            finally:
                process.terminate();process.wait(10)

    check_allow_reload_lockout()

    # Allow list: explicit access-mode, not inferred from the file existing.
    def run_with(name, conf_body, files):
        for fname,body in files.items():
            (root/fname).write_text(body)
        conf=root/(name+'.conf')
        conf.write_text(conf_body)
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        logpath=root/(name+'.log')
        with logpath.open('wb') as log:
            process=subprocess.Popen([exe,'--port',str(port),'--config',str(conf),
                                      '--log',str(root/(name+'-events.log'))],
                                     cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in logpath.read_bytes():
                    if process.poll() is not None:
                        return 'exit %d'%process.returncode, logpath.read_bytes()
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                try:
                    with socket.create_connection(('127.0.0.1',port),timeout=3) as s:
                        s.sendall(wire.packet(1,b''))
                        wire.reply(s,1)
                        return 'connected', logpath.read_bytes()
                except OSError:
                    return 'rejected', logpath.read_bytes()
            finally:
                process.terminate();process.wait(10)

    # A list that exists but is not enforced must say so rather than look active.
    state,text=run_with('allow-open','allowlist=allow-open.txt\n',
                        {'allow-open.txt':'198.51.100.1\n'})
    assert state=='connected',(state,text)
    assert b'NOT enforced' in text,text

    # Enforced: unlisted source refused, with the reason in the log.
    state,text=run_with('allow-deny','allowlist=allow-deny.txt\naccess-mode=allowlist\n',
                        {'allow-deny.txt':'198.51.100.1\n'})
    assert state=='rejected',(state,text)
    assert b'not-in-allowlist' in text,text
    assert b'scope=control' in text,text

    # Enforced: listed source accepted.
    state,text=run_with('allow-permit','allowlist=allow-permit.txt\naccess-mode=allowlist\n',
                        {'allow-permit.txt':'127.0.0.1\n'})
    assert state=='connected',(state,text)

    # Deny must beat permit: listed and banned stays banned.
    state,text=run_with('allow-both',
                        'allowlist=allow-both-a.txt\nbanlist=allow-both-b.txt\naccess-mode=allowlist\n',
                        {'allow-both-a.txt':'127.0.0.1\n','allow-both-b.txt':'127.0.0.1\n'})
    assert state=='rejected',(state,text)
    assert b'banned-source' in text,text

    # Enforced with no usable rules would lock everyone out: refuse to start.
    state,text=run_with('allow-empty','allowlist=allow-empty.txt\naccess-mode=allowlist\n',
                        {'allow-empty.txt':'# empty\n'})
    assert state.startswith('exit '),(state,text)

    # Scoped allow rules: the startup check is per served port, so a file of
    # relay-only rules must not start on a control-only master - it would look
    # configured while nobody could register.
    state,text=run_with('allow-scope-relay','allowlist=allow-scope-relay.txt\naccess-mode=allowlist\n',
                        {'allow-scope-relay.txt':'127.0.0.1 scope relay\n'})
    assert state.startswith('exit '),(state,text)
    assert b'control port' in text,text
    # A control-scoped permit serves the control port but grants nothing on
    # relay, which is the point of the scope.
    state,text=run_with('allow-scope-control','allowlist=allow-scope-control.txt\naccess-mode=allowlist\n',
                        {'allow-scope-control.txt':'127.0.0.1 scope control\n'})
    assert state=='connected',(state,text)
    # A permit whose deadline has already passed is not a usable rule either:
    # counting it would start a master that nobody can reach.
    state,text=run_with('allow-scope-expired',
                        'allowlist=allow-scope-expired.txt\naccess-mode=allowlist\n',
                        {'allow-scope-expired.txt':'127.0.0.1 until 2020-01-01T00:00:00Z\n'})
    assert state.startswith('exit '),(state,text)

    # Temporary ban: expires while the master runs, without touching the file.
    def check_expiring_ban():
        import datetime
        bans=root/'ban-temp.txt'
        conf=root/'ban-temp.conf'
        conf.write_text('banlist=%s\n'%bans)
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        logpath=root/'ban-temp.log'

        def ok():
            try:
                with socket.create_connection(('127.0.0.1',port),timeout=3) as s:
                    s.sendall(wire.packet(1,b''))
                    wire.reply(s,1)
                    return True
            except OSError:
                return False

        def utc(offset):
            return (datetime.datetime.now(datetime.timezone.utc)
                    + datetime.timedelta(seconds=offset)).strftime("%Y-%m-%dT%H:%M:%SZ")

        # Expires soon: banned now, allowed once the deadline passes.
        bans.write_text('127.0.0.1 until %s\n' % utc(4))
        with logpath.open('wb') as log:
            process=subprocess.Popen([exe,'--port',str(port),'--config',str(conf),
                                      '--log',str(root/'ban-temp-events.log')],
                                     cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in logpath.read_bytes():
                    assert process.poll() is None, logpath.read_bytes()
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                assert not ok(), 'ban-temp: a rule with a future deadline must ban'
                # Wait past the deadline; no file edit, no reload.
                end=time.monotonic()+12
                while time.monotonic()<end and not ok():
                    time.sleep(.2)
                assert ok(), 'ban-temp: ban did not expire on its own'
            finally:
                process.terminate();process.wait(10)

        # A deadline in the past must never ban, and must not be a parse error.
        bans.write_text('127.0.0.1 until 2020-01-01T00:00:00Z\n')
        state,text=run_with('ban-past','banlist=ban-past.txt\n',{'ban-past.txt':'127.0.0.1 until 2020-01-01T00:00:00Z\n'})
        assert state=='connected',(state,text)

    check_expiring_ban()
    # bind= in the config persists the operator's choice; loopback stays the
    # default, and an explicit --bind must still win over the config.
    for expected,extra in (('0.0.0.0',[]),('127.0.0.1',['--bind','127.0.0.1'])):
        config.write_text('bind=0.0.0.0\n')
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        logpath=root/'bind.log'
        with logpath.open('wb') as log:
            process=subprocess.Popen([exe,'--port',str(port),'--config',str(config)]+extra,
                                     cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in logpath.read_bytes():
                    assert process.poll() is None
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                text=logpath.read_bytes()
                assert ('listening on %s:'%expected).encode() in text,text
                # The loopback hint must appear only when remote clients really
                # cannot connect, otherwise it becomes noise operators ignore.
                assert (b'bound to loopback' in text)==(expected=='127.0.0.1'),text
            finally:
                process.terminate();process.wait(10)
    for mode in ('off','map','manual','dns-failure'):
        mapped=mode in ('map','manual')
        config.write_text({'off':'# inert\n','map':'address-map=127.0.0.1/32=203.0.113.9\n',
            'manual':'public-address-mode=manual\npublic-ip=203.0.113.9\nhairpin-subnet=127.0.0.1/32\n',
            'dns-failure':'public-address-mode=dns\npublic-host=localhost\nhairpin-subnet=127.0.0.1/32\n'}[mode])
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
        with (root/'stdout.log').open('wb') as log:
            cmd=[exe,'--port',str(port),'--log',str(root/'events.log')]
            if mode!='off':cmd+=['--config',str(config)]
            process=subprocess.Popen(cmd,cwd=root,stdout=log,stderr=log)
            try:
                deadline=time.monotonic()+10
                while b'listening on' not in (root/'stdout.log').read_bytes():
                    assert process.poll() is None
                    assert time.monotonic()<deadline
                    time.sleep(.02)
                def request(op,body=b''):
                    with socket.create_connection(('127.0.0.1',port),timeout=4) as s:
                        s.sendall(wire.packet(op,body));return wire.reply(s,op)
                if mode=='dns-failure':
                    # Only the OS localhost resolver is used; no Internet in CI.
                    start=time.monotonic();assert request(1)[0]==0;assert time.monotonic()-start<1
                    assert request(2,wire.info())[0]==4
                    assert not wire.listing(request(1)[1])
                    continue
                status,lease=request(2,wire.info());assert status==0
                row=wire.listing(request(1)[1])[0]
                assert row[16:20]==socket.inet_aton('203.0.113.9' if mapped else '127.0.0.1')
                assert row[24:156]==wire.info()
                assert request(3,lease[:40])[0]==0
                assert request(4,lease[:40])[0]==0
                assert wire.listing(request(1)[1])==[]
                if mapped:
                    events=(root/'events.log').read_text()
                    assert 'mapped=yes' in events and 'advertised=203.0.113.9:2342' in events
                    assert lease[16:40].hex() not in events
            finally:
                process.terminate();process.wait(10)
    print('Native master: default unchanged, explicit config mapping, source-bound heartbeat/remove and diagnostics OK')
