import socket
import struct
import time
import random
import wave

DEST_IP = "172.35.183.230"  # IP máy phát HF
DEST_PORT = 5060  # Port máy phát HF

SAMPLE_RATE = 8000  # 8 kHz
FRAME_DURATION = 0.02  # 20 ms per frame
SAMPLES_PER_FRAME = int(SAMPLE_RATE * FRAME_DURATION)

# --- G.711 µ-Law encoding ---
def linear2ulaw(sample):
    BIAS = 0x84
    CLIP = 32635
    if sample < 0:
        sign = 0x80
        sample = -sample
    else:
        sign = 0
    if sample > CLIP:
        sample = CLIP
    sample += BIAS
    seg = 0
    mask = 0x4000
    for i in range(8):
        if sample & mask:
            seg = i
            break
        mask >>= 1
    uval = (seg << 4) | ((sample >> (seg + 3)) & 0x0F)
    return (~(uval ^ sign)) & 0xFF

# --- UDP socket setup ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# --- RTP fields ---
version = 2
padding = 0
extension = 0
cc = 0
pt = 0  # G.711 µ-Law
sequence_number = random.randint(0, 65535)
timestamp = random.randint(0, 0xFFFFFFFF)
ssrc = random.randint(0, 0xFFFFFFFF)

MARKER_PTT_ON = 1
MARKER_PTT_OFF = 0

print(f"Phát audio từ test.wav tới {DEST_IP}:{DEST_PORT}")
print("PTT ON (Marker=1) trong khi phát. Nhấn Ctrl+C để PTT OFF.")

try:
    with wave.open('test.wav', 'rb') as wf:
        if wf.getnchannels() != 1 or wf.getsampwidth() != 2 or wf.getframerate() != SAMPLE_RATE:
            print("File WAV phải là mono, 16-bit, 8kHz.")
            sock.close()
            exit(1)
        data = wf.readframes(wf.getnframes())  # Đọc toàn bộ data (little-endian signed 16-bit)

    position = 0
    while True:
        chunk_size = SAMPLES_PER_FRAME * 2
        chunk = data[position:position + chunk_size]
        position = (position + chunk_size) % len(data)  # Loop khi hết file

        if len(chunk) < chunk_size:
            chunk += b'\x00' * (chunk_size - len(chunk))

        payload = bytearray()
        for j in range(0, len(chunk), 2):
            sample = struct.unpack_from('<h', chunk, j)[0]
            payload.append(linear2ulaw(sample))

        # --- Marker bit luôn = 1 khi đang phát ---
        marker = MARKER_PTT_ON

        # --- RTP Header ---
        b0 = (version << 6) | (padding << 5) | (extension << 4) | cc
        b1 = (marker << 7) | pt
        rtp_header = struct.pack("!BBHII", b0, b1, sequence_number, timestamp, ssrc)

        # --- Gửi gói RTP ---
        packet = rtp_header + payload
        sock.sendto(packet, (DEST_IP, DEST_PORT))

        # --- Cập nhật sequence/timestamp ---
        sequence_number = (sequence_number + 1) % 65536
        timestamp = (timestamp + SAMPLES_PER_FRAME) % 0xFFFFFFFF

        time.sleep(FRAME_DURATION)

except KeyboardInterrupt:
    print("\nPTT OFF – gửi gói cuối để nhả PTT...")
    # --- Gửi gói cuối với marker = 0 (PTT OFF) ---
    payload = bytearray([0] * SAMPLES_PER_FRAME)
    marker = MARKER_PTT_OFF
    b0 = (version << 6) | (padding << 5) | (extension << 4) | cc
    b1 = (marker << 7) | pt
    rtp_header = struct.pack("!BBHII", b0, b1, sequence_number, timestamp, ssrc)
    packet = rtp_header + payload
    sock.sendto(packet, (DEST_IP, DEST_PORT))
    sock.close()
    print("Đã ngừng phát audio (PTT OFF).")