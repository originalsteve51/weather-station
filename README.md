# weather-station
Code for my home-made weather station

Based on two esp8266-nodemcu modules and a handful of sensors, this weather station has an outside unit with outside sensors, and an inside unit that has sensors plus a small tft display. The outside unit is set up as a Wifi access point. The inside unit connects to the access point and issues requests via Http to the outside unit. Data is passed back in JSON format. The inside unit parses the data and displays it every 30 seconds. History is kept (max/min, etc) for the duration of time that the inside unit is kept powered up.
