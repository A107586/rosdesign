import sys
if sys.prefix == '/usr':
    sys.real_prefix = sys.prefix
    sys.prefix = sys.exec_prefix = '/home/zj/chapt2/demo_pyphon_pkg/install/demo_pyphon_pkg'
