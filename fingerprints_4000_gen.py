# Tạo file fingerprints_4000.txt với 4000 dòng userid|base64
import base64
import os

with open('fingerprints_4000.txt', 'w') as f:
    for i in range(1, 4001):
        userid = f'user{i:04d}'
        # Tạo dữ liệu vân tay giả (32 bytes random, encode base64)
        fake_fingerprint = base64.b64encode(os.urandom(32)).decode('ascii')
        f.write(f'{userid}|{fake_fingerprint}\n')
