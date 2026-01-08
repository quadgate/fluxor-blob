import base64
import subprocess
import tempfile
import os

# Đường dẫn file fingerprints_4000.txt
input_file = 'fingerprints_4000.txt'
blobstore_bin = './bin/blobstore'
blob_root = 'root'

with open(input_file, 'r') as f:
    for line in f:
        line = line.strip()
        if not line:
            continue
        userid, b64 = line.split('|', 1)
        # Giải mã base64 và ghi ra file tạm
        with tempfile.NamedTemporaryFile(delete=False) as tmp:
            tmp.write(base64.b64decode(b64))
            tmp_path = tmp.name
        # Lưu vào blobstore với key là userid
        subprocess.run([blobstore_bin, 'put', blob_root, userid, tmp_path], check=True)
        os.remove(tmp_path)
print('Đã lưu xong 4000 vân tay vào blobstore!')
