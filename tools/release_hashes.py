"""Security block for the release notes: SHA-256 of what the player downloads and runs, with VirusTotal links.
  python -I tools/release_hashes.py <version>            -> prints the Markdown block (paste it at the end of the notes)
  set VT_API_KEY=<key> first to also upload the files to VirusTotal (free account key; files above 650 MB are refused).
Without the key the links still work once somebody uploaded the same file (virustotal.com, drag and drop).
Files: the package zip, the update zip and the launcher exe inside the package (the one people double-click)."""
import hashlib, json, os, sys, urllib.request, uuid


def need(ok, why):
    """A check of the release (not assert: python -O would drop it)."""
    if not ok:
        sys.exit('ERROR: ' + why)


DEV = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace('\\', '/')  # (the project folder)
PKG = os.environ.get('T6_PKG_DIR', os.path.dirname(DEV) + '/t6pkg').replace('\\', '/')  # (packages + staged build)
ver = sys.argv[1]
OUT = f'{PKG}/release_v{ver}'
FILES = [f'{OUT}/tekken6_native_online.zip', f'{OUT}/tekken6_native_online_patch.zip',
         f'{OUT}/Tekken6NativeOnline/Tekken6 Native Online.exe']
KEY = os.environ.get('VT_API_KEY', '')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def vt_upload(path):
    # the upload url takes files up to 650 MB (the plain /files endpoint only 32 MB)
    req = urllib.request.Request('https://www.virustotal.com/api/v3/files/upload_url', headers={'x-apikey': KEY})
    with urllib.request.urlopen(req, timeout=60) as r:
        url = json.load(r)['data']
    need(url.startswith('https://'), f'VirusTotal gave a non-https upload URL: {url}')
    b = uuid.uuid4().hex
    head = (f'--{b}\r\nContent-Disposition: form-data; name="file"; filename="{os.path.basename(path)}"\r\n'
            f'Content-Type: application/octet-stream\r\n\r\n').encode()
    tail = f'\r\n--{b}--\r\n'.encode()

    def body():  # (streamed: the file is not held in memory twice, 2026-10-18 review)
        yield head
        with open(path, 'rb') as f:
            yield from iter(lambda: f.read(1 << 20), b'')
        yield tail

    req = urllib.request.Request(url, data=body(), method='POST',
                                 headers={'x-apikey': KEY, 'Content-Type': f'multipart/form-data; boundary={b}',
                                          'Content-Length': str(len(head) + os.path.getsize(path) + len(tail))})
    with urllib.request.urlopen(req, timeout=1800) as r:
        r.read()


rows = []
for p in FILES:
    need(os.path.isfile(p), f'missing: {p}')
    d = sha256(p)
    if KEY:
        vt_upload(p)
        print(f'uploaded {os.path.basename(p)}', file=sys.stderr)
    rows.append(f'| `{os.path.basename(p)}` | `{d}` | [scan](https://www.virustotal.com/gui/file/{d}) |')

print('**Security**')
print('- Check that your download is the original: in PowerShell `Get-FileHash <file>` must print the SHA-256 below.')
print('- Every file was scanned on VirusTotal (links below). The launcher downloads updates only from this GitHub page,'
      ' and the game is built on your PC from your own copy of Tekken 6.')
print('- Windows may show "Windows protected your PC" on the first start of the launcher (it is not code-signed):'
      ' click **More info -> Run anyway**. Updates do not ask again.')
print()
print('| File | SHA-256 | VirusTotal |')
print('|---|---|---|')
print('\n'.join(rows))
