import serial
import uinput

ser = serial.Serial('/dev/ttyACM0', 115200)

# Create new mouse device
device = uinput.Device([
    uinput.BTN_LEFT,
    uinput.BTN_RIGHT,
    uinput.REL_X,
    uinput.REL_Y,
    uinput.KEY_SPACE,
    uinput.KEY_ENTER,
    uinput.KEY_UP,
    uinput.KEY_DOWN,
])


def parse_data(data):
    axis = data[0]  # 0 for X, 1 for Y
    value = int.from_bytes(data[1:3], byteorder='little', signed=True)
    print(f"Received data: {data}")
    print(f"axis: {axis}, value: {value}")
    return axis, value


# def move_mouse(axis, value):
#     if axis == 0:    # X-axis
#         device.emit(uinput.REL_X, value)
#     elif axis == 1:  # Y-axis
#         if value > 0:
#             device.emit(uinput.KEY_UP, 1)
#         elif value < 0:
#             device.emit(uinput.KEY_DOWN, 1)
#         else: # caso a tecla n tenha sido apertada 
#             device.emit(uinput.KEY_UP, 0)
#             device.emit(uinput.KEY_DOWN, 0)


try:
    # sync package
    while True:
        print('Waiting for sync package...')
        while True:
            data = ser.read(1)
            if data == b'\xff':
                break                

        # Read 4 bytes from UART
        data = ser.read(3)
        axis, value = parse_data(data)

        if axis == 103: ## UP
            # move_mouse(axis, value)
            device.emit(uinput.KEY_UP, value)
        elif axis == 108: ## DOWN
            device.emit(uinput.KEY_DOWN, value)
        elif axis == 28: ## ENTER
            device.emit(uinput.KEY_ENTER, value)
        elif axis == 57: ## PODER
            device.emit(uinput.KEY_SPACE, value) 


except KeyboardInterrupt:
    print("Program terminated by user")
except Exception as e:
    print(f"An error occurred: {e}")
finally:
    ser.close()
