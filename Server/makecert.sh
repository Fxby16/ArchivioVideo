#!/bin/bash

openssl genpkey -algorithm RSA -out key.pem -aes256
openssl req -new -key key.pem -out server.csr -config openssl.cnf
openssl x509 -req -in server.csr -signkey key.pem -out cert.pem -extensions v3_ca -extfile openssl.cnf
