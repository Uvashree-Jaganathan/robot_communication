from launch import LaunchDescription
from launch_ros.actions import Node
import os


def generate_launch_description():
    model_path = os.path.expanduser(
        "~/robot/src/whisper.cpp/models/ggml-base.en.bin"
    )

    return LaunchDescription([
        Node(
            package="whisper_ros2",
            executable="whisper_transcription_node",
            name="whisper_transcription_node",
            output="screen",
            parameters=[
                {
                    "model_path": model_path,
                    "n_threads": 4,
                    "language": "en",
                    "translate": False,
                    "save_files": True,
                    "save_directory": "~/robot/recordings",
                    "sample_rate": 16000,
                }
            ],
        )
    ])

