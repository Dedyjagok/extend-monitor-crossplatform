import sys
print(f"Python Executable: {sys.executable}")
print(f"sys.path: {sys.path}")
try:
    import numpy
    print(f"numpy version: {numpy.__version__}")
    print(f"numpy path: {numpy.__file__}")
except ImportError as e:
    print(f"Error importing numpy: {e}")

try:
    import cv2
    print(f"cv2 version: {cv2.__version__}")
except ImportError as e:
    print(f"Error importing cv2: {e}")
