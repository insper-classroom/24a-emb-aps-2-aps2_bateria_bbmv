import uinput
import time


# Create new mouse device
device = uinput.Device([
    uinput.BTN_LEFT,
    uinput.BTN_RIGHT,
    uinput.REL_X,
    uinput.REL_Y,
    uinput.KEY_SPACE,
])




while True:
	time.sleep(10)
	print('apertar')  
	device.emit(uinput.KEY_SPACE, 1)        
	time.sleep(2)  
	device.emit(uinput.KEY_SPACE, 0)                         
	
	
	