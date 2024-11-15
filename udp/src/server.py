import socket

size = 8192
sequence_number = 0

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', 9876))

try:
  while True:
    data, address = sock.recvfrom(size)
    response = str(sequence_number) + ' ' + data.decode()
    sequence_number += 1
    sock.sendto(response.encode(), address)
finally:
  sock.close()
