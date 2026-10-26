import rclpy
from rclpy.node import Node
from std_msgs.msg import String
import subprocess
import os
import time

class PiperNode(Node):
    def __init__(self):
        super().__init__('piper_node')
        
        # 1. Define the paths to your Piper binary and model
        # We use the absolute path to your external folder
        self.piper_path = "/home/uva/Desktop/robot_communication/external/piper/piper/piper"
        self.model_path = "/home/uva/Desktop/robot_communication/external/piper/en_US-amy-medium.onnx"
        
        # 2. Create a subscriber to listen to the LLM response
        self.subscription = self.create_subscription(
            String,
            '/llm_response',
            self.listener_callback,
            10)
        
        self.get_logger().info("Piper Node has started. Waiting for LLM responses to speak...")

    def listener_callback(self, msg):
        text_to_speak = msg.data
        self.get_logger().info(f"Speaking: {text_to_speak}")
        
        start_time = time.time()
        try:
            # This is the magic command that connects the text to the speaker
            # We use subprocess.Popen to run the shell command we tested earlier
            command = (
                f'echo "{text_to_speak}" | '
                f'{self.piper_path} --model {self.model_path} --output_raw | '
                f'ffplay -nodisp -autoexit -ar 22050 -ac 1 -f s16le -'
            )
            
            # Run the command in the system shell
            subprocess.run(command, shell=True, check=True)
            
            end_time = time.time()
            duration = (end_time - start_time) * 1000
            self.get_logger().info(f"Piper Processing Time: {duration:.2f} ms")
            
        except Exception as e:
            self.get_logger().error(f"Failed to speak: {str(e)}")

def main(args=None):
    rclpy.init(args=args)
    node = PiperNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
