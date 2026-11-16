# Descrição: compila o pacote público com MSVC x64, SFML e PDFium sem caminhos de máquina fixos.
"""Build on Windows with existing dependencies. No downloads or printer changes."""
import argparse
import hashlib
import importlib.metadata
import importlib.util
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent


def vcvars_path(explicit):
    if explicit:
        return Path(explicit).resolve()
    vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    if not vswhere.is_file():
        raise RuntimeError('Install Visual Studio C++ Build Tools, or provide --vcvars.')
    install = subprocess.check_output([
        str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-property', 'installationPath', '-utf8',
    ], text=True, encoding='utf-8').strip()
    if not install:
        raise RuntimeError('No Visual Studio installation with x64 C++ tools found.')
    return Path(install) / 'VC/Auxiliary/Build/vcvars64.bat'


def quote(path):
    value = str(path)
    # These paths enter a batch file; reject shell expansions, not just whitespace.
    if any(c in value for c in '"%&|<>^!\r\n'):
        raise ValueError('Build paths must not contain shell metacharacters: ' + value)
    return '"' + value + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sfml', required=True, type=Path, help='Extracted SFML 2.6.x MSVC x64 SDK')
    parser.add_argument('--out', type=Path, default=ROOT / 'build', help='Output directory (default: build)')
    parser.add_argument('--vcvars', help='Optional path to vcvars64.bat; otherwise use vswhere')
    args = parser.parse_args()
    if os.name != 'nt' or platform.architecture()[0] != '64bit':
        parser.error('Use Windows x64 and 64-bit Python 3.11 or newer.')
    sfml, out = args.sfml.resolve(), args.out.resolve()
    vcvars = vcvars_path(args.vcvars)
    spec = importlib.util.find_spec('pypdfium2_raw')
    if spec is None or spec.origin is None:
        parser.error('Install the PDFium runtime: python -m pip install pypdfium2')
    pdfium = Path(spec.origin).parent / 'pdfium.dll'
    framework = Path(os.environ['WINDIR']) / 'Microsoft.NET/Framework64/v4.0.30319'
    refs = [framework / 'WPF' / f for f in ('WindowsBase.dll', 'PresentationCore.dll', 'PresentationFramework.dll', 'ReachFramework.dll')]
    refs += [framework / 'System.IO.Compression.dll', framework / 'System.Xaml.dll']
    required = [vcvars, sfml / 'include/SFML/Graphics.hpp', sfml / 'lib/sfml-graphics.lib', pdfium,
                framework / 'csc.exe', *refs, ROOT / 'assets/printer2.png', sfml / 'license.md']
    for path in required:
        if not path.is_file():
            parser.error('Missing dependency: ' + str(path))
    # Validate before writing or invoking the batch file.
    for path in (ROOT, out, sfml, vcvars):
        quote(path)
    out.mkdir(parents=True, exist_ok=True)
    app = out / 'app'
    app.mkdir(exist_ok=True)
    (app / 'assets').mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'assets/printer2.png', app / 'assets/printer2.png')
    shutil.copy2(ROOT / 'LICENSE', app / 'LICENSE-PDVPrinterSim.txt')
    shutil.copy2(sfml / 'license.md', app / 'LICENSE-SFML.md')
    for name in ('sfml-graphics-2.dll', 'sfml-window-2.dll', 'sfml-system-2.dll'):
        shutil.copy2(sfml / 'bin' / name, app / name)
    shutil.copy2(pdfium, app / 'pdfium.dll')
    dist = importlib.metadata.distribution('pypdfium2')
    licenses = [f for f in (dist.files or []) if '.dist-info/licenses/' in str(f).replace('\\', '/')]
    if not licenses:
        raise RuntimeError('PDFium distribution licenses were not found; refusing to bundle without notices.')
    for source in licenses:
        suffix = str(source).replace('\\', '/').split('.dist-info/licenses/', 1)[1]
        destination = app / 'licenses-pdfium' / suffix
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dist.locate_file(source), destination)
    compiler = f'cl /nologo /std:c++17 /EHsc /utf-8 /O2 /MD /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I{quote(sfml / "include")}'
    libraries = f'/LIBPATH:{quote(sfml / "lib")} sfml-graphics.lib sfml-window.lib sfml-system.lib'
    commands = [
        '@echo off', f'call {quote(vcvars)} >nul', 'if errorlevel 1 exit /b 1',
        f'{compiler} /Fo{quote(out / "main.obj")} {quote(ROOT / "main.cpp")} /link /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup {libraries} ws2_32.lib /OUT:{quote(app / "PDVPrinterSim.exe")}',
        'if errorlevel 1 exit /b 1',
        f'{compiler} /Fo{quote(out / "bounds.obj")} {quote(ROOT / "testar-enquadramento.cpp")} /link {libraries} /OUT:{quote(app / "testar-enquadramento.exe")}',
        'exit /b %errorlevel%',
    ]
    batch = out / 'compile.cmd'
    batch.write_text('\n'.join(commands) + '\n', encoding='utf-8')
    result = subprocess.run(['cmd.exe', '/d', '/c', str(batch)], cwd=ROOT, capture_output=True,
                            text=True, encoding='utf-8', errors='replace', creationflags=subprocess.CREATE_NO_WINDOW)
    (out / 'build.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError('C++ compilation failed. See ' + str(out / 'build.log'))
    subprocess.run([str(framework / 'csc.exe'), '/nologo', '/target:exe', '/optimize+',
                    '/out:' + str(app / 'RenderXps.exe'), *('/reference:' + str(p) for p in refs),
                    str(ROOT / 'RenderXps.cs')], cwd=ROOT, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    hashes = {p.relative_to(app).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in sorted(app.rglob('*')) if p.is_file() and 'capturas' not in p.parts}
    (out / 'manifest.json').write_text(json.dumps({'pdfium_package': dist.version, 'sha256': hashes}, indent=2) + '\n', encoding='utf-8')
    print('Built: ' + str(app / 'PDVPrinterSim.exe'))


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(str(exc))
