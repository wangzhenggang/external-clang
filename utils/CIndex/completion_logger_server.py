#!/usr/bin/env python
import sys
from socket import *
from time import localtime, strftime

def main():
  if len(sys.argv) < 4:
    print "completion_logger_server.py <listen address> <listen port> <log file>"
    exit(1)

  host = sys.argv[1]
  port = int(sys.argv[2])
  buf = 1024 * 8
  addr = (host,port)
  
  # Create socket and bind to address
  UDPSock = socket(AF_INET,SOCK_DGRAM)
  UDPSock.bind(addr)
  
  print "Listing on {0}:{1} and logging to '{2}'".format(host, port, sys.argv[3])

  # Open the logging file.
  f = open(sys.argv[3], "a")

  # Receive messages
  while 1:
    data,addr = UDPSock.recvfrom(buf)
    if not data:
      break
    else:
      f.write(strftime("'%a, %d %b %Y %H:%M:%S' ", localtime()))
      f.write("'{0}' ".format(addr[0]))
      f.write(data)
      f.write('\n')
      f.flush()

  # Close socket
  UDPSock.close()

if __name__ == '__main__':
  main()
