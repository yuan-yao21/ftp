import socket
 
size = 8192
 
try:
  sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
  for i in range(0,51):
    msg = str(i)
    sock.sendto(msg.encode(), ('localhost', 9876))
    response = sock.recv(size)
    print(response.decode())
  sock.close()

except:
    print("Cannot reach the server")
