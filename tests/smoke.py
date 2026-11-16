# Descrição: testa o executável público via TCP local e o leitor XPS com documentos sintéticos.
"""No printer/driver changes. Runs only against the simulator process it starts."""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time

from fixtures import pdf, xps


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def wait_until(callback, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        result = callback()
        if result:
            return result
        time.sleep(.05)
    raise TimeoutError('Simulator did not produce the expected result.')


def connect():
    try:
        return socket.create_connection(('127.0.0.1', 9100), timeout=1)
    except OSError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=Path(__file__).resolve().parents[1] / 'build/app')
    parser.add_argument('--report', type=Path, help='Optional JSON report path')
    args = parser.parse_args()
    app = args.app.resolve()
    require((app / 'PDVPrinterSim.exe').is_file(), 'Build the standalone simulator first.')
    require(os.name == 'nt', 'This executable test requires Windows.')
    # Fail before starting or sending any data if another service owns the port.
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        probe.bind(('127.0.0.1', 9100))
    cases = []
    with tempfile.TemporaryDirectory(prefix='pdvprintersim-qa-', dir=app.parent) as folder:
        isolated = Path(folder) / 'app'
        shutil.copytree(app, isolated, ignore=shutil.ignore_patterns('capturas'))
        capture = isolated / 'capturas'
        capture.mkdir()
        process = subprocess.Popen([str(isolated / 'PDVPrinterSim.exe'), '--headless'], cwd=isolated,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            ready = wait_until(connect)
            ready.close()
            require(process.poll() is None, 'The isolated simulator exited during startup.')

            def send(payload, fragment=False):
                before = (capture / 'ultimo.json').read_bytes() if (capture / 'ultimo.json').exists() else b''
                with socket.create_connection(('127.0.0.1', 9100), timeout=5) as client:
                    if fragment:
                        for start in range(0, len(payload), 3):
                            client.sendall(payload[start:start + 3])
                            time.sleep(.001)
                    else:
                        client.sendall(payload)
                    client.shutdown(socket.SHUT_WR)

                def received():
                    try:
                        data = (capture / 'ultimo.json').read_bytes()
                        return json.loads(data) if data != before else None
                    except (FileNotFoundError, json.JSONDecodeError):
                        return None
                return wait_until(received)

            text = b'\x1b@DEMO SHOP\n\x1bE\x01Item|9.90\n\x1bE\x00TOTAL|9.90\x0cEND WITHOUT LF'
            job = send(text, fragment=True)
            require(job['formato'] == 'texto', 'Expected ESC/POS text.')
            require((capture / (job['id'] + '.raw')).read_bytes() == text, 'Original text bytes changed.')
            saved = (capture / (job['id'] + '.txt')).read_text()
            require('DEMO SHOP\n' in saved and 'TOTAL|9.90\nEND WITHOUT LF\n' in saved, 'Fragmented text was lost.')
            cases.append('Fragmented ESC/POS, form feed and trailing text')
            for format_name, factory in (('pdf', pdf), ('xps', xps)):
                for width in (58, 80):
                    payload = factory(width_mm=width, blank_first=True, pages=2)
                    job = send(payload, fragment=True)
                    require(job['formato'] == format_name and job['paginas'] == 2, 'Wrong format/page count.')
                    require((capture / (job['id'] + '.' + format_name)).read_bytes() == payload, 'Original document changed.')
                    for number, measure in enumerate(job['medidas_mm'], 1):
                        require(abs(measure['largura'] - width) < .02 and abs(measure['altura'] - 120) < .02, 'Page dimensions changed.')
                        require(measure['vazio'] == (number == 1), 'Blank-page detection failed.')
                        require(measure['sobraInferiorMm'] > 90, 'Blank tail was cropped or measured incorrectly.')
                        png = (capture / (job['id'] + f'-p{number}.png')).read_bytes()
                        require(png.startswith(b'\x89PNG\r\n\x1a\n'), 'Page PNG missing.')
                        pw, ph = struct.unpack('>II', png[16:24])
                        require(abs(pw / ph - width / 120) < .01, 'Preview aspect ratio changed.')
                    cases.append(f'{format_name.upper()} {width}mm: exact originals, two pages, blank page and tail')
            for name, options in (('external', {'external': True}), ('page-limit', {'pages': 21}),
                                  ('dimensions', {'height_mm': 10000})):
                path = capture / f'trabalho-rejected-{name}.xps'
                path.write_bytes(xps(**options))
                result = subprocess.run([str(isolated / 'RenderXps.exe'), str(path)], capture_output=True,
                                        timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
                require(result.returncode != 0 and not path.with_suffix('.medidas').exists(), 'Invalid XPS accepted: ' + name)
                cases.append('Reject XPS: ' + name)
            outside = Path(folder) / 'trabalho-outside.xps'
            outside.write_bytes(xps())
            rejected = subprocess.run([str(isolated / 'RenderXps.exe'), str(outside)], capture_output=True,
                                      timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
            require(rejected.returncode != 0, 'XPS outside capture folder accepted.')
            cases.append('Reject XPS outside captures')
            subprocess.run([str(isolated / 'testar-enquadramento.exe')], check=True, timeout=10,
                           creationflags=subprocess.CREATE_NO_WINDOW)
            cases.append('Content bounds and blank-tail unit tests')
            previous = (capture / 'ultimo.json').read_bytes()
            with socket.create_connection(('127.0.0.1', 9100), timeout=5) as stalled:
                stalled.sendall(b'INCOMPLETE RECEIPT')
                time.sleep(16)
            require((capture / 'ultimo.json').read_bytes() == previous, 'Timed-out partial job was printed.')
            require(send(b'AFTER TIMEOUT\n')['formato'] == 'texto', 'Server did not recover after timeout.')
            cases.append('Reject timed-out partial job and recover for the next job')
        finally:
            process.terminate()
            process.wait(timeout=10)
    report = {'passed': len(cases), 'cases': cases, 'scope': 'Synthetic TCP/rendering tests; no physical printer or Windows queue exercised.'}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
