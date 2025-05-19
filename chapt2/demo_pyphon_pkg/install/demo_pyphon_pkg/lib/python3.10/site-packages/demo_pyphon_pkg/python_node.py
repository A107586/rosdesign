import rclpy
from rclpy.node import Node
def main():
    rclpy.init()
    node = Node('python_node')
    node.get_logger().info('Hello, Python!')
    node.get_logger().warn('This is a Python node.')
    rclpy.spin(node)
    rclpy.shutdown()
