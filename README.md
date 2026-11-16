# PDVPrinterSim — thermal print capture for Windows

A local printer simulator built with C++ and SFML, originally created by
[Victor Hugo Manata Pontes](https://github.com/vhpontes/PDVPrinterSim).

This contribution extends the original ESC/POS viewer to capture **PDF and XPS**
from Windows print queues. It displays the **actual page dimensions** and keeps
the original document, so a 58 mm receipt, an 80 mm receipt, an unexpected A4
page, a rotated receipt, and a long blank tail can be investigated separately.

**Em português:** simulador local de impressora térmica com captura de texto
ESC/POS, PDF e XPS. Mostra o tamanho real recebido e a sobra de papel em branco,
sem corrigir ou esconder erros de escala, orientação ou comprimento do documento.

## Features

- Raw TCP input on **127.0.0.1:9100**, restricted to this computer.
- Buffers a complete connection before parsing, including commands split across
  TCP packets. One TCP connection is one job; close or half-close the write side
  after sending. Incomplete jobs that time out are rejected.
- Basic ESC/POS text: justification, bold, underline, character size and cut.
  This is a limited parser, not full ESC/POS bitmap/barcode/device emulation.
- PDF rendering through PDFium, loaded from the application directory.
- XPS rendering through Windows WPF in a separate process with a 30-second timeout.
- Multipage preview, scroll, blank-page detection and blank-tail length in mm.
- Original `.raw`, `.pdf` or `.xps` files, PNG previews and `capturas/ultimo.json`.
- `--headless` mode and independent tests with synthetic documents.

The listener accepts at most 32 MiB per job, waits at most 15 seconds for the next
TCP data, and renders at most 20 pages. XPS rejects external resources and has
decompression and pixel limits. Use trusted local test documents: this application
is a debugging tool, not a sandbox for arbitrary files.

## Build on Windows x64

Prerequisites:

- Python **3.11+**, 64-bit.
- Visual Studio C++ Build Tools, with the x64 C++ toolset and Windows SDK.
- **SFML 2.6.x**, the **MSVC x64** SDK; SFML 3 is not source-compatible here.
- .NET Framework 4.x with WPF (normally included in Windows).
- PDFium runtime from `pypdfium2`.

Download and extract the matching SFML SDK, then run from the repository root:

```powershell
python -m pip install pypdfium2
python build.py --sfml "C:\Libraries\SFML-2.6.2"
python tests/smoke.py --report build/smoke-report.json
.\build\app\PDVPrinterSim.exe
```

The builder finds Visual Studio with `vswhere`; `--vcvars` can specify a
`vcvars64.bat` explicitly. `--out` chooses a different build directory. Paths
containing command-shell metacharacters are rejected. It does not download tools,
install drivers, create queues, change the default printer or require elevation.

`build/app` contains the executable, SFML/PDFium DLLs, the XPS helper, the original
printer image and third-party licenses. Keep these together. An MSVC-built binary
requires the matching Microsoft Visual C++ x64 runtime on another computer.
Build logs and a file-hash manifest are written under `build`.

The smoke suite needs no print driver and sends only fictitious documents to
the simulator process it starts in a temporary directory. It checks fragmented
text, PDF/XPS 58/80 mm, multipage/blank output, original bytes, page proportions,
XPS rejection cases, content bounds, partial-job timeout and recovery. It refuses
to start if TCP port 9100 is occupied. These are transport/renderer tests, **not**
a physical printer or Windows spooler certification.

## Send a raw test job

Start the simulator, then:

```powershell
python -c "import socket; s=socket.create_connection(('127.0.0.1',9100)); s.sendall(b'\x1b@DEMO SHOP\nItem|9.90\nTOTAL|9.90\n\x1dV\x01'); s.close()"
```

The GUI also has an **Imprimir Teste** button. To send an existing local PDF or
XPS, send its complete bytes through the same TCP connection and close it.
Documents are rendered at their original size; the simulator does not convert
A4 to thermal paper or automatically rotate/crop the captured originals.

## Test browser or application printing

1. Start the simulator on the same Windows computer as the application.
2. Create a **separate test printer queue** with a Standard TCP/IP port pointing
   to `127.0.0.1`, RAW port `9100`; disable SNMP for this local test port.
3. Select an already-installed driver that emits **XPS, PDF or plain text**.
   For thermal tests it must support custom paper sizes. A driver producing
   proprietary binary commands is not supported by this renderer.
4. Set the test queue's paper size to 58 mm or 80 mm and an explicit height,
   such as 300 mm. Confirm portrait orientation. Do not change a real queue or
   the system default printer; select the test queue explicitly when printing.
5. Print a fictitious receipt from the browser or POS application. Inspect the
   received dimensions, PNG and original file under `build/app/capturas`.

The signed **RustDesk v4 Printer Driver**, when already present, was used during
development in two dedicated loopback queues: it accepted 58×300 and 80×300 mm
and produced XPS. No RustDesk remote session/service or original printer queue
was used. This repository does not bundle or install a particular driver.
Choose a driver you already trust and confirm its actual output.

Microsoft Print to PDF may normalize custom paper sizes to A4 on some systems.
That is a driver capability/setting, **not a limitation of the PDF format**.
Generic / Text Only is suitable for raw text tests but cannot validate the
browser's graphical receipt faithfully.

Browser CSS, print-dialog settings and driver capabilities all affect the
result. If a 300 mm page contains only a short receipt, the unused length remains
visible in the capture. If the browser rotates a receipt, the PNG keeps that
rotation. Correct those problems at the source/driver and print again.

## Preview controls

| Control | Action |
| --- | --- |
| Mouse wheel | Scroll the document vertically |
| Left / Right | Previous / next page |
| `0` | Full original page width |
| `5` / `8` | Virtual 58 / 80 mm crop for inspecting a larger page |
| `A` / `D` | Move a virtual crop horizontally |

The initial view follows the first visible content. Scroll upward to inspect
any leading blank area. **A virtual crop is only a viewing aid**; original files,
page measurements and captured PNGs are never cropped. A successful capture does
not mean the layout is correct or that a real printer will behave identically.

## Captures and privacy

Captures stay on this computer and contain whatever was printed. Prefer test
data; do not commit real receipts. `build/`, `capturas/`, dependencies and binaries
are ignored by Git. Delete your own test captures when no longer needed.
The application makes no requests to a POS API or external service.

## License and attribution

MIT; see [LICENSE](LICENSE). The original copyright notice and printer artwork
are preserved. SFML and PDFium retain their own licenses, copied beside the
built application. No POS application, customer data or commercial integration
code is needed to build and test this repository.
