import serial
import struct
import time
ser = serial.Serial('/dev/tty.usbserial-AH022AI3',115200)

#Connect to VladBootin
array = bytearray()
array.append(0x16)
ser.write(array)
print("Sended SYN to VladBootin")

time.sleep(1)

f = open('../vladBootin.img','rb')
f.seek(0,2)
size = f.tell()
print("Kernel image is " + str(size) + " byte")
bytes = size.to_bytes(4,'little')
ser.write(bytes)

time.sleep(1)

f.seek(0,0)
file = f.read()
b = bytearray(file)
ser.write(b)

ser.close()

