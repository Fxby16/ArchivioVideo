# How to run the server and client
## Create a certificate to authenticate the server
1. `cd Server`
2. Open `openssl.cnf` and substitute commonName and IP.1 with your server's IP address.
3. Create the certificate
    - Windows
        ```bash
        .\makecert.bat
        ```
    - Linux
        ```bash
        ./makecert.sh
        ```
4. Copy `cert.pem` and `key.pem` in the directory from which you will run the server (e.g. `Server`).

## Update the Android client
1. Replace `res/raw/cert.pem` content with the content of the `cert.pem` file you just created.
2. Replace the IP address in `res/xml/network_security_config.xml` with your server's IP address.
3. Replace the server's IP address in `res/values/strings.xml` with your server's IP address.

## Run the server
1. Open a terminal and navigate to the `Server` directory.
2. Run the server
    - Windows
        Run the server from Visual Studio or run the binary located in `Server/bin/Debug/` or `Server/bin/Release/`.
    - Linux
        ```bash
        bin/{Debug,Release}/ArchivioVideoServer
        ```
3. The server will listen on port 10000 for HTTPS and 10001 for HTTP.

## Run the client
Just run the app on your Android device. Make sure the device is connected to the same network as the server. The app will automatically detect the server's IP address and connect to it.