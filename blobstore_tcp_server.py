import socket
import subprocess
import threading

HOST = '0.0.0.0'  # Lắng nghe trên mọi địa chỉ
PORT = 9000       # Cổng TCP
BLOBSTORE_BIN = './bin/blobstore'
BLOBSTORE_ROOT = 'root'

# Xử lý từng client
def handle_client(conn, addr):
    try:
        conn.sendall(b'Welcome to Blobstore TCP Service!\n')
        while True:
            data = conn.recv(4096)
            if not data:
                break
            cmd = data.decode().strip().split()
            if not cmd:
                continue
            # Chỉ hỗ trợ các lệnh đơn giản: put/get/exists/list/rm/stat
            if cmd[0] == 'put' and len(cmd) == 3:
                key, filedata_b64 = cmd[1], cmd[2]
                import base64, tempfile, os
                with tempfile.NamedTemporaryFile(delete=False) as tmp:
                    tmp.write(base64.b64decode(filedata_b64))
                    tmp_path = tmp.name
                result = subprocess.run([BLOBSTORE_BIN, 'put', BLOBSTORE_ROOT, key, tmp_path], capture_output=True)
                os.remove(tmp_path)
                conn.sendall(result.stdout + result.stderr)
            elif cmd[0] == 'get' and len(cmd) == 2:
                key = cmd[1]
                import tempfile, base64, os
                with tempfile.NamedTemporaryFile(delete=False) as tmp:
                    tmp_path = tmp.name
                result = subprocess.run([BLOBSTORE_BIN, 'get', BLOBSTORE_ROOT, key, tmp_path], capture_output=True)
                if result.returncode == 0:
                    with open(tmp_path, 'rb') as f:
                        filedata_b64 = base64.b64encode(f.read())
                    conn.sendall(filedata_b64 + b'\n')
                else:
                    conn.sendall(result.stdout + result.stderr)
                os.remove(tmp_path)
            elif cmd[0] in ['exists', 'stat', 'rm'] and len(cmd) == 2:
                result = subprocess.run([BLOBSTORE_BIN, cmd[0], BLOBSTORE_ROOT, cmd[1]], capture_output=True)
                conn.sendall(result.stdout + result.stderr)
            elif cmd[0] == 'list':
                result = subprocess.run([BLOBSTORE_BIN, 'list', BLOBSTORE_ROOT], capture_output=True)
                conn.sendall(result.stdout + result.stderr)
            else:
                conn.sendall(b'Invalid command\n')
    except Exception as e:
        conn.sendall(f'Error: {e}\n'.encode())
    finally:
        conn.close()

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((HOST, PORT))
        s.listen()
        print(f'Blobstore TCP server listening on {HOST}:{PORT}')
        while True:
            conn, addr = s.accept()
            threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()

if __name__ == '__main__':
    main()
