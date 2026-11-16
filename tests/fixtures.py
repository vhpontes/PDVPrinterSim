# Descrição: gera documentos térmicos fictícios em memória, sem aplicativos ou dados privados.
"""Small, synthetic PDF/XPS fixtures using only the Python standard library."""
import io
import zipfile

NS = 'http://schemas.microsoft.com/xps/2005/06'


def xps(width_mm=58, height_mm=120, blank_first=False, pages=1, external=False):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('[Content_Types].xml', '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                         '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
                         '<Default Extension="fdseq" ContentType="application/vnd.ms-package.xps-fixeddocumentsequence+xml"/>'
                         '<Default Extension="fdoc" ContentType="application/vnd.ms-package.xps-fixeddocument+xml"/>'
                         '<Default Extension="fpage" ContentType="application/vnd.ms-package.xps-fixedpage+xml"/></Types>')
        extra = '<Relationship Id="bad" Target="https://example.invalid/never-fetch" TargetMode="External" Type="urn:test"/>' if external else ''
        archive.writestr('_rels/.rels', '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
                         '<Relationship Id="r1" Type="http://schemas.microsoft.com/xps/2005/06/fixedrepresentation" Target="/sequence.fdseq"/>' + extra + '</Relationships>')
        archive.writestr('sequence.fdseq', f'<FixedDocumentSequence xmlns="{NS}"><DocumentReference Source="/document.fdoc"/></FixedDocumentSequence>')
        archive.writestr('document.fdoc', f'<FixedDocument xmlns="{NS}">' + ''.join(f'<PageContent Source="/page{i}.fpage"/>' for i in range(pages)) + '</FixedDocument>')
        for i in range(pages):
            # An asymmetric black shape makes rotation and blank-tail errors observable.
            shape = '' if blank_first and i == 0 else '<Path Fill="#ff000000" Data="M 10,10 L 100,10 100,30 25,30 25,60 10,60 Z"/>'
            archive.writestr(f'page{i}.fpage', f'<FixedPage xmlns="{NS}" xml:lang="en-US" Width="{width_mm * 96 / 25.4}" Height="{height_mm * 96 / 25.4}">{shape}</FixedPage>')
    return buffer.getvalue()


def pdf(width_mm=58, height_mm=120, blank_first=False, pages=1):
    width, height = width_mm * 72 / 25.4, height_mm * 72 / 25.4
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'']
    kids = []
    for page in range(pages):
        page_id = len(objects) + 1
        kids.append(f'{page_id} 0 R')
        objects.append(f'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {width} {height}] /Contents {page_id + 1} 0 R >>'.encode('ascii'))
        drawing = b'' if blank_first and page == 0 else f'0 g 8 {height - 30} 70 20 re f\n8 {height - 45} 12 20 re f\n'.encode('ascii')
        objects.append(f'<< /Length {len(drawing)} >>\nstream\n'.encode('ascii') + drawing + b'endstream')
    objects[1] = f'<< /Type /Pages /Kids [{" ".join(kids)}] /Count {pages} >>'.encode('ascii')
    data = bytearray(b'%PDF-1.4\n')
    offsets = [0]
    for i, value in enumerate(objects, 1):
        offsets.append(len(data))
        data += f'{i} 0 obj\n'.encode('ascii') + value + b'\nendobj\n'
    xref = len(data)
    data += f'xref\n0 {len(offsets)}\n0000000000 65535 f \n'.encode('ascii')
    for offset in offsets[1:]:
        data += f'{offset:010} 00000 n \n'.encode('ascii')
    data += f'trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode('ascii')
    return bytes(data)
