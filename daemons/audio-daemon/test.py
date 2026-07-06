# test.py (receiver)
import socket
import struct
import sys

def draw_bar(value, width=50):
    filled = int(max(0.0, min(1.0, value)) * width)
    return '█' * filled + ' ' * (width - filled)

def main():
    # В Linux абстрактные сокеты начинаются с нулевого байта ('\0')
    socket_name = "\0shader-desk-audio"
    print("Listening for audio data on abstract socket: @shader-desk-audio")

    sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)

    try:
        sock.bind(socket_name)
        print("Socket created. Waiting for data... (Press Ctrl+C to exit)")

        # Формат структуры AudioData:
        # uint32_t magic (4 байта)
        # float volume, bass, mid, treble (4 * 4 = 16 байт)
        # float bands[64] (64 * 4 = 256 байт)
        # Итого: 276 байт. Little-endian (<), I (uint32), 68f (68 флоатов)
        struct_format = '<I68f'
        expected_size = struct.calcsize(struct_format)

        while True:
            data, addr = sock.recvfrom(65536)
            if not data:
                continue
                
            if len(data) != expected_size:
                print(f"Warning: Received {len(data)} bytes, expected {expected_size} bytes.")
                continue

            try:
                # Распаковываем бинарные данные
                unpacked = struct.unpack(struct_format, data)
                
                magic = unpacked[0]
                if magic != 0x41554431: # Проверка "AUD1"
                    print("Warning: Magic number mismatch!")
                    continue

                volume = unpacked[1]
                bass = unpacked[2]
                mid = unpacked[3]
                treble = unpacked[4]
                bands = unpacked[5:] # Оставшиеся 64 элемента

                # Отрисовка в терминале
                print("\033[H\033[J", end="")
                print("--- Audio Visualization ---")
                print(f"Volume: [{draw_bar(volume)}] {volume:.2f}")
                print(f"  Bass: [{draw_bar(bass)}] {bass:.2f}")
                print(f"   Mid: [{draw_bar(mid)}] {mid:.2f}")
                print(f"Treble: [{draw_bar(treble)}] {treble:.2f}")

                if bands:
                    # Печатаем спектр (объединяем каждые 2 полосы для компактности в терминале)
                    cols = min(64, len(bands))
                    step = max(1, len(bands) // cols)
                    spec = ''.join('▮' if bands[i] > 0.15 else ' ' for i in range(0, len(bands), step))
                    print("\nSpectrum (coarse):")
                    print(spec)

            except struct.error as e:
                print(f"Error unpacking message: {e}")

    except KeyboardInterrupt:
        print("\nCaught Ctrl+C, shutting down.")
    finally:
        print("Closing socket.")
        sock.close()

if __name__ == "__main__":
    main()
