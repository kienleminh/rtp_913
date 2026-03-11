#include <cstdint> // For uint8_t and int16_t
#include <cmath>   // For sin() and M_PI
#include <cstdio>  // For printf()
#include <cstring> // For memset(), memcpy()
#include <QByteArray>
#include <QHostAddress>
#include <QUdpSocket>
#include <QThread>

#define SAMPLE_RATE 8000
#define TONE_FREQUENCY 1000
#define TONE_DURATION_SECONDS 10
#define FRAME_DURATION 0.02
#define SAMPLES_PER_FRAME int(SAMPLE_RATE * FRAME_DURATION)
#define DEST_IP "172.35.183.230"
#define DEST_PORT 123

#define VERSION 2
#define PADDING 0
#define EXTENSION 0
#define CSRC_COUNT 0
#define MARKER 1
#define PAYLOAD_TYPE 0 // PCMU

uint8_t linear2ulaw(int16_t sample) {
    const int BIAS = 0x84;
    const int CLIP = 32635;
    uint8_t sign = 0;
    if (sample < 0) {
        sign = 0x80;
        sample = -sample;
    }
    if (sample > CLIP) {
        sample = CLIP;
    }
    sample += BIAS;
    int seg = 0;
    int mask = 0x4000;
    for (int i = 0; i < 8; ++i) {
        if (sample & mask) {
            seg = i;
            break;
        }
        mask >>= 1;
    }
    uint8_t uval = static_cast<uint8_t>((seg << 4) | ((sample >> (seg + 3)) & 0x0F));
    return (~(uval ^ sign)) & 0xFF;
}

int main() {
    // Generate a 1 kHz tone for 10 seconds
    int16_t samples[SAMPLE_RATE * TONE_DURATION_SECONDS];
    for (int i = 0; i < SAMPLE_RATE * TONE_DURATION_SECONDS; ++i) {
        samples[i] = static_cast<int16_t>(32767 * sin(2 * M_PI * TONE_FREQUENCY * i / SAMPLE_RATE));
    }

    QUdpSocket udpSocket;
    QHostAddress destinationAddress(QStringLiteral(DEST_IP));
    if (destinationAddress.isNull()) {
        printf("Invalid destination IP: %s\n", DEST_IP);
        return 1;
    }

    uint16_t sequence_number = static_cast<uint16_t>(rand() % 65536);
    uint32_t timestamp = static_cast<uint32_t>(rand());
    uint32_t ssrc = static_cast<uint32_t>(rand());
    const int total_frames = (SAMPLE_RATE * TONE_DURATION_SECONDS) / SAMPLES_PER_FRAME;
    QByteArray packet(12 + SAMPLES_PER_FRAME, 0);

    for (int frame = 0; frame < total_frames; ++frame) {
        uint8_t* data = reinterpret_cast<uint8_t*>(packet.data());
        data[0] = (VERSION << 6) | (PADDING << 5) | (EXTENSION << 4) | CSRC_COUNT;
        data[1] = (MARKER << 7) | PAYLOAD_TYPE;

        data[2] = static_cast<uint8_t>((sequence_number >> 8) & 0xFF);
        data[3] = static_cast<uint8_t>(sequence_number & 0xFF);

        data[4] = static_cast<uint8_t>((timestamp >> 24) & 0xFF);
        data[5] = static_cast<uint8_t>((timestamp >> 16) & 0xFF);
        data[6] = static_cast<uint8_t>((timestamp >> 8) & 0xFF);
        data[7] = static_cast<uint8_t>(timestamp & 0xFF);

        data[8] = static_cast<uint8_t>((ssrc >> 24) & 0xFF);
        data[9] = static_cast<uint8_t>((ssrc >> 16) & 0xFF);
        data[10] = static_cast<uint8_t>((ssrc >> 8) & 0xFF);
        data[11] = static_cast<uint8_t>(ssrc & 0xFF);

        const int sample_offset = frame * SAMPLES_PER_FRAME;
        for (int i = 0; i < SAMPLES_PER_FRAME; ++i) {
            data[12 + i] = linear2ulaw(samples[sample_offset + i]);
        }

        qint64 sent = udpSocket.writeDatagram(packet, destinationAddress, DEST_PORT);
        if (sent != packet.size()) {
            printf("writeDatagram failed: %s\n", udpSocket.errorString().toLocal8Bit().constData());
            return 1;
        }

        if (frame < 5) {
            printf("Sent RTP packet #%u to %s:%d\n", sequence_number, DEST_IP, DEST_PORT);
        }

        ++sequence_number;
        timestamp += SAMPLES_PER_FRAME;

        QThread::msleep(static_cast<long>(FRAME_DURATION * 1000));  // 20 ms delay
    }

    // Send final packet with MARKER=0 for PTT OFF
    uint8_t* data = reinterpret_cast<uint8_t*>(packet.data());
    data[1] = (0 << 7) | PAYLOAD_TYPE;  // MARKER=0
    memset(data + 12, 0xFF, SAMPLES_PER_FRAME);  // Silence in μ-law
    udpSocket.writeDatagram(packet, destinationAddress, DEST_PORT);

    printf("Done sending 1 kHz tone RTP stream to %s:%d\n", DEST_IP, DEST_PORT);

    return 0;
}