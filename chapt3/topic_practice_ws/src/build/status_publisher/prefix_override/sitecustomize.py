import sys
if sys.prefix == '/usr':
    sys.real_prefix = sys.prefix
    sys.prefix = sys.exec_prefix = '/home/zj/chapt3/topic_practice_ws/src/install/status_publisher'
