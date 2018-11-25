#!/usr/bin/python

from socket import *

for i in range(1, 200):
	c = create_connection(('127.0.0.1',6666))
	c.send("a"*200)
	data = c.recv(4096);