make -j 12 all && curl -v $1:8032 --data-binary @- < build/ESP32clock.bin
