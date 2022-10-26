# Hls_streamer

# 1.	Installation process
In main folder hls_streamer:
```bash
mkdir build
cd ./build
cmake ..
make
```
To run
```bash
./app_main
```

# 2.	Instructions
Please update properties in source_properties.ini file as follows  
1. num_sources: Number of cameras to run  
2. rtsp_uri_x: rtsp url of source stream  
3. hls_uri_x: url to start http server and publish hls stream  

You can check stream using vlc as follows:
if hls_uri_x=http://192.168.10.1:8080 then stream will be live at this url http://192.168.10.1:8080/live

# 3.	OS dependencies
## You need to have Jetpack 4.6 or 5.02 installed with following:
#### Deepstream 6
You can install deepstream depending on jetpack as follows:
```bash
sudo apt-get install -y deepstream-6.0
or
sudo apt-get install -y deepstream-6.1
```
