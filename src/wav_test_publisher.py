import wave
import time
import rclpy
from rclpy.node import Node
from audio_common_msgs.msg import AudioData


class WavPublisher(Node):
    def __init__(self):
        super().__init__('wav_test_publisher')

        self.publisher = self.create_publisher(
            AudioData,
            '/speech_audio',
            10
        )

        self.wav_path = (
            '/home/uva/robot/src/whisper.cpp/samples/jfk.wav'
        )

        self.wav = wave.open(self.wav_path, 'rb')

        self.sample_rate = self.wav.getframerate()
        self.channels = self.wav.getnchannels()
        self.sample_width = self.wav.getsampwidth()

        self.get_logger().info(
            f'WAV: {self.sample_rate} Hz, '
            f'{self.channels} channel(s), '
            f'{self.sample_width * 8}-bit'
        )

        self.chunk_frames = 16000

        self.timer = self.create_timer(
            0.5,
            self.publish_chunk
        )

        self.finished = False

    def publish_chunk(self):
        if self.finished:
            return

        data = self.wav.readframes(self.chunk_frames)

        if not data:
            self.get_logger().info('Finished publishing WAV')
            self.finished = True
            self.wav.close()
            self.timer.cancel()
            return

        msg = AudioData()
        msg.data = list(data)

        self.publisher.publish(msg)

        self.get_logger().info(
            f'Published {len(data)} bytes'
        )


def main():
    rclpy.init()

    node = WavPublisher()

    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass

    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
